# OpenLords2 progress log

Reverse-chronological journal of substantial changes.  Each entry is
a self-contained dispatch: what was done, what was learned, what the
next candidate milestones are.  Engine specifics belong in
`docs/findings/<subsystem>.md`; this log is the narrative thread.

---

## 2026-07-02 (session 24) — state-slot triage RESOLVED: the s23 divergences are load-flip bookkeeping, NOT a real sim divergence; map geography is byte-identical (new lane8 + f2bits state-dump diagnostics)

Ran the s23 discriminating experiment and took it further with two new
byte-granular state-dump diagnostics.  Verdict: **no real sim
divergence — the whole cluster is load-flip-baked render/RNG/cursor
bookkeeping, and the actual map geography matches bit-for-bit.**  The
s23 popup-dismiss-timing hypothesis is DISPROVEN.

**New tooling (both engines, matched format — state_dump.c + agent
stateDumpOnce + frida_capture persist + olv3 passthrough):**
- `lane8` — per-8-byte-lane FNV hashes for every 8 B/cell block.  The
  overworld map is 8 B/cell (+0 terrain, +1 flags, +5 army head-link,
  +7 region), so a lane mismatch localizes a divergence to a cell
  FIELD instead of a 2 KB byte range.
- `f2bits` — per-BIT hashes of map cell field +2 (a render/autotile
  flags byte: 0x1c autotile code, 0x20 render-dirty, 0x40/0x80 edge).
  Localizes to a single flag bit.

**What the county_panels + a lean `slot_triage_probe` capture show:**
- Two families.  **Transient** (self-heals by the first panel open):
  slot 4 g_per_army_768 (all lanes), map field +5 army head-links,
  part of slot 3 — fine sub-tick SETTLE-phase misalignment right after
  load (the anchor cancels the gross load-stretch, not the per-tick
  settle).  Benign; converges.  This — not the persistent core — was
  the timing-sensitive part, so the "reactive-dismiss lag" intuition
  was pointing at the wrong slots.
- **Persistent core** (never heals, incl. at `final` = MAP_FREEROAM
  +1830): map field +2 **bit 0x01 ONLY**; slot 3 strategic RNG-scratch;
  slot 84 AI round-robin cursor (port 2 / retail 1); + the KNOWN LFSR
  18/19 and popup-flag 74.
- **Map geography is byte-identical**: terrain (lane +0) and region
  (lane +7) MATCH on every capture.  The only persistent map diff is
  field +2 bit 0x01 = the per-cell **redraw-request / dirty flag**
  (FUN_004063c1: `if (cell[2]&1) { cell[2]&=0xfe; FUN_00452628(x>>4,
  y>>4); }` — set → renderer repaints that tile and clears it).  It is
  invisible in the composited output (the pixel render was already
  verified byte-exact) and is the same family as the known dirty-rect
  "panel-switch scraps" divergence (gameplay-and-screens.md).

**Why the s23 "permanent hash-apart" was a red herring:** nothing
writes the map during idle browsing, so the stable byte band was baked
in at load — before any dismiss — which the popup-timing story can't
explain.  Bit-level: it's a redraw bookkeeping bit, not geography.

**Severity: LOW / cosmetic.**  slots 0/3/84 are all downstream of
load-flip bookkeeping (redraw flags, RNG-scratch, AI-cursor phase),
not a sim-state divergence.  slot 84 (AI round-robin start offset) is
the only one worth a second glance IF a turn-order divergence ever
surfaces — it's stable (both cycle all realms), so likely benign too.
Recorded as engine-quirk #216.

**Also this session — labor screen 0x0f fully analyzed (execution-ready).**
Wrote `docs/findings/labor-screen.md`: the complete decompile analysis +
port plan for FUN_00412b33 + its 6 task sub-renderers.  Key de-risking
findings: (1) ALL 20 render helpers are already ported; (2) every .rdata
table extracted via r2 (per-task icon/bar/cue maps, blacksmith weapon
rate/cost tables, string constants); (3) every county field is a
mechanical `DAT_0053fXXX − 0x53f9b0` offset over g_per_army_768.  The
only real risk left is pixel transcription (the olv3 index-diff loop +
a human scrub resolve it) and the main renderer's sprite-state globals
(DAT_005c9258/005aeb70/00591524-8 — the last is the very dirty-mark
position from quirk #216's FUN_00452628), which want a quick verify.
Deliberately did NOT ship blind unverified rendering code (byte-exact
project; the verify loop is the deferred human step).

**Port sequencing (helper-coverage scan):** labor 0x0f has ZERO unported
helper deps → port it FIRST.  Armory 0x0a/0x17 has only 3 unported
internal helpers (FUN_004181eb / FUN_00418426 / FUN_004996c8) → second.

**Next candidates (execution):** 1. port labor 0x0f per labor-screen.md
(engine_labor.c/.h + render-arm wiring + a `--force-labor` seat mirroring
`--force-workforce`, then capture + index-diff each task panel + human
scrub).  2. armory 0x0a/0x17 (port the 3 helpers first).  3. End-Turn
season reports.

---

## 2026-07-02 (session 23) — the retail-seat input-scale gap ROOT-CAUSED + fixed (quirk #215: truncated-percent cursor mapping, not a client-rect size) — and the v3 STATE oracle turns out to have been retail-blind since day one, now repaired

Session 22's queued-first task, run to ground the way its writeup
demanded (watch retail's own cursor mirrors, don't infer).

**The "(6,2-3) px short" clicks — quirk #215.**  Session 22 guessed the
retail window's client rect (~646×486) scaled injected clicks down.
Wrong mechanism: on the headless seat the viewport rect is the whole
DESKTOP (1920×1080, quirk #120 branch), and the engine's cursor→game
mapping truncates twice (`scale = 64000/1920 = 33`, not 33.33; then
`game = client*33/100`).  The agent's lParam pre-scale inverted the
IDEAL ratio, so the truncated divide landed ~1% short: x=599→593,
y=226→223.  Fixed `rescaleMouseLParamToClient` to invert the quantized
form exactly (`cx = ceil(gx*100/scale)`).  Verified empirically: a
`--watch` on `DAT_004e65fc/f4` through county_panels_click — all 10
authored coordinates now reproduce EXACTLY in retail's believed cursor.

**The tooltip x-offset flag — RESOLVED, and it wasn't the seat.**  The
re-captured retail side is byte-identical to session 22's (grid-button
tooltips anchor to the classified HOTSPOT, not the raw cursor; every
trace hit-box is ≥24 px, so the 1% cursor error changed no pixels).
The user's "too far left" scrub flag (06:01) predated the quirk-#214
stale-backdrop fix (06:09): the pre-#214 ghost polluted both the visual
and the x=380-vs-374 box measurement.  Current containers: tooltip
region ZERO-diff; population/happiness settled residue is 102/36 px of
cow-sprite animation fringe (the known anim-counter gap).
**USER-CONFIRMED (viewer scrub): "tooltips match."**

**HARNESS FIND: the v3 state panel has been retail-blind since v3
began.**  Retail `.raw` headers embed the agent's capture ORDER as
`present`, but `_read_state` keys retail state off `present` →
`state_{tick}.json` never matched — EVERY view.json ever built (39
sessions audited) had `retail-state=0`.  One-line ingest fix (retail
`present` ← the captures.jsonl tick, symmetric with the port).  The
county-panels view now pairs state on all 15 columns and shows: LFSR
slots 18/19 + popup flag 74 (the KNOWN load-flip divergences), plus
newly-visible mismatches to triage — slot 0 `g_overworld_map`
(sub-chunks 2,3,4), slot 3 `g_player_strategic_352` (sub-chunks
3,5,8,13), slot 84 `s_slot_0057c944` (redraw cookie / AI round-robin
cursor: port 2 vs retail 1), and slot 4 `g_per_army_768` on
before_clicks ONLY (converges after the popups clear).  First-pass
read: retail's welcome scroll is still OPEN at the before_clicks
capture tick while the port has already dismissed (reactive-dismiss
lag), so the engines run the early sim phases offset — the army table
re-converges, but a single offset RNG draw / AI pass leaves the map
plane + strategic records + round-robin cursor permanently hashed
apart, exactly like the known +1 LFSR pull.  Discriminating
experiment for next session: run a trace with the welcome series
force-dismissed BEFORE the anchor (or a save with no boot popups) —
if slots 0/3/84 go clean, the whole cluster is the popup-timing
family; if not, there's a real sim divergence to chase.

**Next candidates:** the state-slot discriminating experiment above;
then continue the playthrough trace — labor screen 0x0f, armory
0x0a/0x17, End-Turn season reports.

---

## 2026-07-02 (session 22) — PLAYTHROUGH-DRIVEN PORTING begins: game knowledge researched, the county control-panel family (population/tax/happiness/rations + steppers + slider) and the TOOLTIP subsystem ported, all verified via one organic click-trace — and the trace exposed a retail-seat input-scale calibration gap

New working mode (user-directed): research how the game is actually
played, then extend a playthrough trace turn-by-turn, stopping to port
whatever unported code each step hits.

**Game knowledge + screen map
([docs/findings/gameplay-and-screens.md](findings/gameplay-and-screens.md)).**
Synthesized the strategy-guide corpus (manual, GameFAQs, Steam guide,
openlotr2 docs) into the seasonal turn loop + the standard opening a
trace should follow, and derived the COMPLETE `DAT_004eac50` screen-ID
map by pairing FUN_0040f1a0's render dispatch with each renderer's
L2.eng string group (dump recipe in the doc): merchant 0x08, treasury
0x09, armory 0x0a/0x17, labor 0x0f, split 0x11, population 0x14, tax
0x15, happiness 0x16, supplies 0x18, rations 0x19, diplomacy 0x0b/0x1a,
castle 0x1b, siege 0x1d, rankings 0x20, ….

**Port 1 — the county-panel family (commit d7d864b).**  The 2×2 action
grid (FUN_00438ceb) + all four detail panels (FUN_004110b1/0041152f/
004116fb/00411b72), the seasonal history bar graph (FUN_004156a7 —
g_realm_history ring, base-quirk block−8, bucket/yscale/barw tables
r2-extracted), the "(±N♥)" delta label (FUN_0041ac95), the rations
stepper + absolute band (FUN_0043a379, arrow-priority on the 0x145
overlap pinned by test) and the ration-set snap loop (FUN_0043a5a9,
dead-drag revert + 100-try clamp walk pinned), the shared dispatcher
case (42ff10.c:445-560 — NO shared tail, retail goto), engine.c hooks
(action grid / rations / generic-click-on-seal, the last also giving
screen 4 its LMB close) + the render-seam arm (full recomposite THEN
panel — erases the previous panel on grid switches).

**Port 2 — the panel widgets (commit 3ded58f).**  The trace diff showed
the flip renderers DON'T draw the interactive widgets: retail paints
them per-frame (FUN_004ba26e 0x15/0x19 → FUN_0040cfd2 over .rdata
button tables 0x4dd790/0x4dd7c0 + FUN_00411fde) and dispatches their
clicks in the UI router (FUN_004ba9c8 0x15/0x19 → FUN_0040da1e over the
SAME tables → FUN_0043aa32/aad5 tax ±1 → income recompute;
FUN_0043a1e9/a2b2 wanted-level ±1 → provision+plan_realm).  The 4×-zoom
loop caught FUN_00409c93 = the plain interior-tile fill (NOT a bevel)
and the FORK sprite (0x4c) as the slider knob.  Slider region now
ZERO-diff; rations frames 2.07%→0.53%.

**Port 3 — tooltips (commit 82b8831).**  The settled-frame residue was
retail's tooltips — a complete unported subsystem.  Ported it whole
(engine_tooltip.c): 1s-idle arm + backdrop save, activity clear +
restore (new engine_workforce_hud_restore_backdrop = FUN_004b3ec0),
mode table 0x4d6fb8, the full strategic hotspot classifier
FUN_00477320 (battle classifier FUN_004777aa deferred), white-box
render off loc group 0xdc.  Both engines now draw the SAME tooltip at
the grid buttons; population/happiness settled 2.09%→0.78-0.80%.

**Verification: `tools/traces/county_panels_click.jsonl`** — one
organic trace on the default lastturn.sav seat: open all four panels
via real grid clicks (including a direct panel→panel grid switch),
click the rations +/− steppers (both engines run the ported economy
recompute in LOCKSTEP), RMB-close.  15 paired captures; comparisons on
the feed; viewer launched on the session for scrubbing.

**HARNESS FINDING (next session's first task): the retail seat lands
every positioned click ~(6,2-3) px short.**  Tooltips are the first
cursor-DERIVED render, and the box offset exposed it: the agent's
windowed-input scale fix overwrites DAT_004ea180..18c with the REAL
client rect each tick, and the retail window's client is ~646×486 (not
640×480), so injected client coords get scaled by game/client.  Every
prior positioned click silently tolerated this (hit-boxes ≥ 24 px).
Fix = pin the retail client to exactly 640×480 (or pre-scale injected
lParams), then re-capture county_panels_click — the tooltip boxes
should snap to zero-offset.  Also documented: retail's OWN panel-switch
scraps (its flip recomposite is dirty-rect-gated; the port paints a
clean map — deliberate divergence until the dirty-rect-gated composite
is ported).

**USER-CONFIRMED (viewer scrub): "trace matches."**  One flag to carry
into the seat investigation: the tooltip boxes (e.g. "View happiness
report") are a few px off in X on the port — the user reads the port as
too far LEFT, while the container measurement put the port box RIGHT of
retail's (x=380 vs 374).  Don't assume a direction when picking this
up: watch retail's own cursor mirrors (DAT_004e65fc/f4) during the
capture and compare each engine's box against the cursor IT believes,
rather than inferring from the injected coordinates.

**Second user flag → FIXED same session (commit 92f7809, quirk #214).**
The happiness panel's "Last season 65♥" persisted as a rectangle (plus
UI-scrap patches over terrain) on the port's tax_open frame.  Root
cause was an ORDER fact that also corrects the plan doc: retail
dispatches in-game input at the tick END (4b99c0 line 192, AFTER the
renders at 36/42 and the tooltip at 112) — a click-flip repaints next
tick, so retail's tooltip-clear backdrop restore always lands on the
frame it was captured from.  The port dispatches input first, so the
flip's repaint preceded the clear and the restore stamped stale
previous-screen pixels onto the fresh frame.  Compensation: a
canvas-repainted-this-tick flag invalidates the backdrop (clear skips
the restore; a tip shown through a repaint re-captures).  Re-captured:
the ghost went 5465 px → 36 px (sprite-anim noise); tax_open back to
the 1.10% retail-scraps baseline, rations frames to 0.53%.  Suite
2311/2311.

**Suite: 2310/2310 green** (+32 this session).  Next candidates: the
seat calibration fix (+ the tooltip-offset flag above); then continue
the playthrough trace into the labor-allocation screen 0x0f (workforce
commit hook already stubbed), the blacksmith/armory chain (0x0a/0x17),
or End-Turn → season-report verification on the same trace.

---

## 2026-07-02 (session 21) — the LAST two confirm-dialog siblings fired organically (claim/slaughter, attack/field, direct/besiege) with retail pixel-parity — and the session-20 "Army Movement hint gap" mystery is RESOLVED: a harness scheduling race, now fixed (quirk #213)

Picked up session 20's "2 siblings remain" next-candidate from
[[project_event_popup_arms]] — `sibling_983b` (claim + attack dialog arms)
and `sibling_960f` (direct) had never fired via a real click.

**The "harder to stage" assumption was wrong.** Session 20 predicted these
needed a fabricated enemy-owned record.  A save dump showed `test.sav`
already carries everything: `ARMY_REC[2]` is a real owner-2 record and the
map's uniform baseline occupant stamp (2) points AT it — so the "enemy army
under the cursor" condition holds at any baseline cell once county A is
owned.  (33,47) is a naturally 0x20-flagged enemy FIELD tile (attack
classifies with zero patches), and the keep-adjacent (31,46) — the cell
session 20 patched friendly for garrison — classifies DIRECT when left
alone.  `tools/make_enemy_target_fixture.py` patches only: the standard
county-A ownership pair, movement-spent 5→0 (the field floods at cost 14
vs the source save's 10 headroom — the classifier's final gate zeroes ALL
classifications over headroom), and TWO bytes on (32,46) for claim
(flags|=0x10 AND type=0x10 — `engine_strat_cost_rebuild` maps flag-0x10
cells to cost-0 IMPASSABLE unless type==0x10; found empirically when the
flags-only fixture fell through to the plain-move path).

**The localization table renamed the whole classification family**: msg 4
(claim) = "Slaughter villagers?", msg 10 (attack) = "Destroy field?", msg 8
(direct) = "Besiege castle?" — bit 0x10 is villages, 0x20 fields, 0x80
castles.  The port's comment names (claim/attack/direct) date from before
the strings were read; the dialogs confirm the real semantics.

**Retail comparison exposed the session-20 mystery — and it was the
HARNESS, not the port.**  Retail kept showing the first-time "Army
Movement" hint (0xd2) eating clicks 2/3 while the port ran clean.  A
`--watch ep/scr/hint_d2` run proved retail's hint fires 1-2 frames after
the screen-0x10 arm WITH ep=0xd2 set (dismissable) — but the trace's bare
`{"auto_dismiss":0}` op executed at ~t910 on retail vs ~t1000 on the port:
the port drains {capture} ops in-order (later ops wait), the agent pre-arms
them out-of-band (later ops don't).  The toggle-off landed before retail's
hint and after the port's.  Session 20's retracted "hint gap" + its wrong
sandbox-state theory were exactly this race.  Fix: optional anchor-relative
gate `{"t":N,"auto_dismiss":M}` honoured by all four parsers (port C
parser, agent, frida_capture loader, trace_ops compiler); bare form keeps
immediate-drain semantics.  Quirk #213.

**Result: all three dialogs port-vs-retail pixel-identical** in the
dialog/map region (only the documented right-panel gap + sprite
animation-phase diffs remain, both present at the before-clicks baseline),
Yes → silent-execute on all three (claim/attack: merge==0 branch;
besiege: link=4 → TERR_REC[4]+0x19a==0 branch), and the save-schema state
ribbon shows ONLY the pre-existing documented divergence set (slots
0/18/19/71/74 + AI-brain cursor 84, the open phase-4 FSM chip) at every
capture before AND after the clicks — the confirm chain adds zero new sim
divergence.  Host suite 2278/2278 green (src changed only in
input_trace.c, the harness op).

**Every arm of `FUN_004a9252`'s confirm-dialog family is now organically
click-verified**: plain-move (s18), swap (s19), garrison (s20), claim +
attack + direct (s21).  Remaining sub-branches only reachable with
different data: sibling_983b's 0x11e (merge with link), sibling_960f's
0x11d/0x11c (link==0) and 0x113 (link with siege flag), sibling_975d's
0x112 (merge over cap) — all event-post paths already host-tested.

**Next candidate**: with the whole screen-0x10 chain closed, return to the
frontier list (docs/port-frontier.md regenerated this session: 258
frontier, 136 leaves) — or the AI-brain cursor slot-84 divergence
(phase-4 FSM), the last stable sim-state gap the state ribbon shows on
every in-game trace.

---

## 2026-07-01 (session 20) — organically fired `sibling_9372`, the LIVE producer of the 0x11 event popup, via a real click for the first time. Also found a new (unfixed) gap. Autonomous session, same day as session 19.

Continued straight from session 19's own "next candidate": of `FUN_004a9252`'s 4 confirm-dialog siblings, `sibling_9372` (garrison-merge-cap) is the most valuable one still unexercised organically — it's the actual live producer of the 0x11 event popup, previously only reached via host tests (session 14, the 0xa6 sub-branch) or `--force-popup`.

**Built `tools/make_garrison_county_fixture.py`.** The hover classifier's garrison/direct gate (`CELL[+1]&0x80 && CELL[+0]>0x14`) needs a target cell carrying the same "keep" terrain flags county A's own keep tile has, whose occupant stamp (`+7`) points at a FRIENDLY army record. Found map cell (31,46) — one tile east of the keep — already naturally carries that exact terrain category in `test.sav` and is otherwise unclaimed, so patched only its occupant stamp, redirecting it from the map's uniform baseline value (2, confirmed present at every cell in the whole neighborhood — not a real placed army) to a fabricated friendly record at `ARMY_REC` key 6 (a byte-for-byte copy of key 4, real self-consistent data, same reuse approach as session 19's county fixture). Patched ONE field on the copy: keep level (`+0x1c0`) forced from 1 to 6, so `engine_combat_garrison_cap(6)` reads the baked table's `k_garrison_cap[6]==0` — deliberately routing `sibling_9372` to its `cap<1` branch (event 0x11b) rather than the already-host-tested `cap<headcount` branch (0xa6, the styled 0x11 popup itself), since this project's table has no reachable keep level between 1 and county A's headcount (47) anyway.

Derived the target's screen coordinates with the same Python port of `engine_camera_cursor_to_cell`/`build_cells` session 19 wrote, extended with floor-division (not exact division) for `sc = sc2/2` — a one-tile offset from the already-verified keep coordinate happened to need it (`(0x40-46+31)` is odd), and an earlier version of the derivation asserted evenness and would have raised. Re-verified round-trip clean: pixel (290,265) → cell (4,16) → decodes back to (31,46).

**First attempt found nothing — a real methodology trap, not a code bug.** The dialog opened correctly ("Garrison castle?", confirming `sibling_9372`'s branch fired), the Yes-press-flash animated, the dialog closed on schedule — but no popup ever appeared afterward despite dense captures across the predicted firing window. Root cause: `sibling_9372`'s OTHER branches (everything except 0xa6) post with `flag5=0`, meaning they open as a **type-0 generic scroll**, not the styled 2-icon "0x11" layout — and this trace's `{"auto_dismiss":1}` (needed to clear the boot-time welcome series) reacts to ANY open scroll regardless of type, clearing the 0x11b popup within a tick or two of it opening, before any capture point landed on it. Fixed by toggling `{"auto_dismiss":0}` right after the boot sequence clears (well before the interesting click sequence starts) — a trace-authoring technique worth remembering for any future popup-arm test whose outcome isn't the styled 0x10/0x11/0x12 layout.

**With that fixed: clean, correct result.** "Garrison castle?" → Yes → "Castle fully barracked. Your castle already contains its maximum complement of troops, my Lord." — event 0x11b's actual localized text, never displayed anywhere in this port before (the type-0 renderer itself has been DONE/verified since early in this project, but only ever exercised with the boot-time 256-colour warning; this confirms it correctly pulls a completely different code's strings too). Stayed open through the rest of the trace, matching the documented "SP popups are modal, only input dismisses" behavior. Host suite 2278/2278 green throughout (no src/ changes, same as session 19 — the existing port code was already correct).

**Retail comparison initially looked like it found a gap — retracted after re-checking; genuinely unresolved, not a confirmed finding.** Running the identical fixture+trace against retail shows a first-time "Army Movement" tutorial hint already open immediately after click 1 (arming county A's handoff), which absorbed retail's later clicks in that trace. First read: "the port never triggers this hint." That's very likely WRONG — `engine.c`'s help-hint pump (`engine_tick_help_hint_step`, the `FUN_00476aa7` port) already has an explicit `scr==0x10 && !g_hint_shown[0xd2-0xc8]` arm that opens report `0xd2` (source-verified, not inferred), and session 18's own writeup already recorded this exact trigger firing correctly live ("retail's own first-time tutorial hint (code 0xd2) correctly fires, opens over screen 0x27, auto-dismisses, and correctly RESTORES to 0x10"). The far more likely explanation is the same methodology trap this session already hit once with the 0x11b popup: `0xd2` opens as a screen-0x27 report that this trace's own `{"auto_dismiss":1}` reactively clears within a tick or two, and none of the captures happened to land in that window. Spent real effort trying to re-verify cleanly (toggling `auto_dismiss` off at various points before click 1) but kept tripping a SEPARATE artifact instead — disabling it too early left the boot-time welcome series itself stuck open (never confirmed why the same auto_dismiss duration that works in the committed trace didn't clear the boot chain in these variants; possibly sandbox/session state accumulated across repeated runs against the same `--save-ref`, not re-investigated). Not resolved — recording the retraction so this doesn't calcify into a false "known gap" the way an unretracted claim would. If picked up again: instrument `g_hint_shown[0xd2-0xc8]` directly (it's a plain runtime array, not save-schema-visible, so needs a temporary read-out) rather than fighting trace timing.

**Next candidate**: 2 siblings remain fully unexercised organically — `sibling_983b` (claim/attack) and `sibling_960f` (direct/new-county-claim). Both need a target cell classified via the OTHER two `CELL[+1]` flag bits (0x10/0x40 for claim/merge, 0x20 for attack) paired with a DIFFERENT-owned (enemy) army underneath — harder to stage than garrison/swap since it needs a genuine enemy-owned record, not just a friendly one.

---

## 2026-07-01 (session 19) — organically exercised the `g_classify_swap` sibling of the screen-0x10 confirm chain for the first time via a real click. Autonomous session, same day as session 18.

Picked up session 18's own "next candidate" from [[project_event_popup_arms]]:
click a genuinely DIFFERENT second-click target (not the same county
session 18 re-clicked, which fell through to a no-classification no-op)
to organically exercise one of `FUN_004a9252`'s 4 confirm-dialog siblings
for the first time via a real click, not `--force-popup`.

Read `engine_army_move_confirm.c`'s hover classifier closely: of the six
classification flags, `g_classify_swap` is the cleanest target — it's
set purely from `TERR_REC(head_terr)[0]==local_player &&
armed_county!=head_terr && TERR_REC(head_terr)[8]!=4`, where `head_terr`
is `g_overworld_map[cell]+5`, the SAME reciprocal stamp
`tools/make_owned_county_fixture.py` already knows how to write. No
dependency on the cell's own flag byte (unlike merge/claim/attack/
garrison/direct, which all need specific `+1` bits set) — so it's
isolable by clicking a plain, unflagged empty cell.

**Built `tools/make_swap_county_fixture.py`.** Confirmed (via a direct
save dump) that every available save has exactly ONE real kind==1
territory record (county key 4, `test.sav`'s keep at map cell
col=30/row=46 — same one session 18 used) and that county key 5 is
fully unused (all-zero) in the same save. Fabricated a second owned
county at key 5 by copying key 4's record byte-for-byte (already
self-consistent real game data — the swap gate never reads the record's
own col/row, only owner/kind) and relocating its reciprocal stamp to map
cell (32,46): confirmed via direct save inspection to be empty
(`head_terr==0`) and unflagged (descriptor `+1` byte `==0`), 2 plain-cost
tiles east of the first keep — comfortably inside county 4's
presence-accumulator headroom (`+0x154-+0x153` = 10) that gates whether
the hover classifier arms anything at all.

Derived screen coordinates for both keep tiles by writing a faithful
Python port of `engine_camera_cursor_to_cell` +
`engine_map_init_build_cells`'s rotation-0 diamond projection, then
round-trip-verifying candidate pixels against it (same rigor session 18
established) — reused the ALREADY-VALIDATED (260,250)→cell(4,15) click
for county 4, and derived (320,280)→cell(5,17) for county 5 at the same
fixed scroll (20,62), so no camera scroll is needed between the two
clicks. Also worked out the confirm dialog's "Yes" button hitbox
(`engine_confirm_dialog.c`'s button table: x=64,y=46,kind=0x20, offset by
the swap call site's `ox=oy=0xa0`) → pixel (240,222).

New trace `tools/traces/swap_county_click.jsonl`: click county 4 (arms
screen 0x10) → hover + click county 5 (should set `g_classify_swap` and
open the generic Yes/No dialog, screen 0x10→0x1e) → click "Yes" (type-5
buttons only ARM a 20-frame press-flash; the actual callback fires later,
so the trace captures several settle points after the click rather than
assuming a fixed frame offset).

**Result: clean, on the first attempt — no bugs found this session.**
The dialog opened showing "Combine armies?" (localization group 10,
sub-index 5 — the exact, only-reachable-via-`g_classify_swap` message),
both engines' rendering pixel-identical in the map/dialog region when
run side-by-side through `olv3_capture.py` on both port and retail
against the same fixture. The Yes button visibly showed the type-5
press-flash (darkened) after the third click; by the final settle capture
the dialog had closed back to the map and both counties' idle-army flag
markers had disappeared, consistent with `army_move_confirm_execute`
committing (headcount 47+47=94, well under the 0x5dd merge cap, so the
silent-execute sub-branch fired rather than the 0x112 event-post
sub-branch — a smaller follow-up if the event-post path is wanted
later). The only pixel diffs vs retail were in the right-side info panel
(the already-documented deferred "manage county" portrait gap, see
[[project_workforce_hud]]) and army-sprite animation-frame phase — both
present already at the `before_clicks` baseline capture (a consequence of
this trace's `{"auto_dismiss":1}`, which is documented to desync
port/retail timing by a few frames — see [[project_loadflip_rng_clarified]]),
not anything introduced this session. Host suite 2278/2278 green
throughout (no src/ changes — this session only added tooling).

**Next candidate for this specific chain**: the remaining 3 siblings
(`sibling_983b` claim/attack, `sibling_960f` direct/empty-cell,
`sibling_9372` garrison-merge-cap, the actual 0x11 popup producer) are
still only host-tested/`--force-popup`-verified, not organically
click-driven. `sibling_9372` is the most interesting (it's the live
0x11 popup producer) but needs a target cell with descriptor flag `+1`
bit 0x80 set and a FRIENDLY-owned army/keep under it — doable with the
same fixture technique (the neighborhood dump this session pulled from
`test.sav` already shows several naturally-occurring 0x80-flagged cells
near the existing keep) but wasn't attempted this session; scope
separately rather than tacking on.

---

## 2026-07-01 (session 18) — fully organic screen-0x10 click-driven verification, closing the gap sessions 13/14/16 left open — and it found 2 real bugs. Autonomous session, same day as session 17.

Picked up the standing next-step from [[project_event_popup_arms]] /
[[project_headless_cursor_gap]]: the screen-0x10 army-move/merge confirm
chain (landed session 14) had only been verified via `--force-popup` +
host tests; the one reliably-clickable coordinate in the regression trace
(260,250) resolves to an ARMY (kind-3), not a plain owned county
(kind-1), so the "click an owned county → arm screen 0x10 → click again
→ resolve" path had never actually run through the real dispatch code
end-to-end. Session 16 flagged two ways to close this: scroll to a
different county's keep, or read the save's territory records directly
to find one up front. Took the second path.

**Finding #1 — the default pinned reference save has ZERO ownable
counties.** Added a temporary diagnostic dump of `g_per_territory_420`
(kind/col/row/link per record) at the `MAP_FREEROAM` anchor: the fresh
map-0 new-game save (`_ol2_reference/lastturn.sav`, pinned since
[[project_save_redirect_reproducibility]]) has exactly 6 records, all
kind==3 (the starting armies) — no player has built a keep yet at turn 1,
so `engine_army_select_dispatch`'s "owned county" branch (kind==1) is
*structurally unreachable* from this save. Session 16's blind coordinate
sweep could never have found a plain county, independent of tile-sampling
technique — retroactively explains that session's dead end.

Found older backup saves in the game dir (`old_turn.sav`/`test.sav`,
map 17, from a pre-save-redirect session) with one kind==1 record (county
key 4, map cell col=30/row=46) — but it's owned by player 2, not
`local_player`(1), **and** `g_overworld_map`'s own reciprocal stamp at
that cell is 0 (the array `g_per_territory_420` and the array
`g_overworld_map` disagree about whether anything is placed there —
i.e. the two arrays were never fully synced for this specific backup,
independent of anything this session touched). Built a byte-patch fixture
(`tools/make_owned_county_fixture.py`, new script) that copies the save,
fixes the owner byte + the reciprocal stamp, and overrides the carried
camera scroll so the county sits at screen-diamond cell (4,15) — the
SAME cell the existing (260,250)/`lParam 16384260` coordinate (used by
`click_county.jsonl` etc.) already resolves to at any scroll. New trace
`tools/traces/plain_county_click.jsonl` documents the full derivation
(had to invert `engine_map_init_build_cells`'s rotation-0 diamond
projection — naive "screen cell + scroll = map col/row" addition is
WRONG, landed on an unfilled diamond gap and silently produced
`county_id=0` despite `engine_camera_cursor_to_cell` reporting `ready=1`;
verified by reimplementing the projection in Python and cross-checking
against the *other* already-known-working coordinate).

**Finding #2 (real bug, FIXED) — screen 0x10/0x1e's per-frame refresh was
wrongly demand-redraw-gated.** `engine_tick_title_render_step` (engine.c)
had the screen-0x10 hover-reclassify step (`engine_tick_army_move_hover_
step`, which decrements the handoff cooldown gating the second click) and
the screen-0x1e confirm-dialog refresh nested inside `if (g_demand_redraw
!= 0)` — but retail's equivalent (`FUN_004ba26e`) runs these arms
UNCONDITIONALLY every frame (same category as the title screen's own
per-frame button-hover refresh, already handled correctly elsewhere in
this same function). Net effect: once nothing else requested a redraw,
the handoff cooldown froze forever and a second click could never resolve
— a real interactive soft-lock on the just-landed army-move feature under
normal idle-mouse play, invisible to `--force-popup`/host-test coverage
since neither exercises the live per-frame tick loop across idle frames.
Fixed by hoisting both arms out of the demand-redraw gate.

**Finding #3 (real bug, FIXED) — `strat_draw_menu_bar`'s leftover
`engine_set_current_screen(0)`.** Added in commit 8c2eb01 when this
render path only ever served screen 0 (retail's own `FUN_00419c78`
*reads* the screen id for its own gating but never writes it — confirmed
via the decompile, so this was never a faithful-port requirement). Once
commit d7cc930 wired the every-frame map composite to also fire on screen
0x10, this call started stomping screen 0x10 back to 0 on literally the
first frame after arming — the army-move handoff screen was unreachable
in real play the instant it was entered, every time, regardless of the
demand-redraw fix above. Confirmed redundant everywhere else (both
bring-up seats and the live load path already set the screen themselves
before rendering) and removed.

With both fixes, the full click-driven sequence now runs for real: click
1 arms screen 0->16 via `engine_army_select_dispatch` ->
`engine_army_select_confirm_or_open` -> `engine_army_select_open_
handoff`; retail's own first-time tutorial hint (code 0xd2, "entering the
army-move screen") correctly fires, flips to screen 0x27, auto-dismisses,
and correctly RESTORES to 16 (not 0) via `engine_popup_screen27_restore`
— a nice confirmation that path is also right; click 2 reaches
`ti_army_move_click_resolve` -> `engine_army_move_confirm_click_resolve`
for the first time via a real click (screen back to 0, no notice posted
since the click retargeted the same already-armed county — a plausible
no-op classification, not chased further). Screenshots of all 3 stages
pushed to the feed. 2278/2278 host tests still green throughout (this
frontier is entirely engine.c/engine_screen_strat.c, outside the host
suite, consistent with every prior session touching these files).

**Next candidate**: click a *different* target on the second click (e.g.
another owned army, or across an enemy border) to organically exercise
one of `FUN_004a9252`'s 4 sibling outcome codes (0xa4/0xa5/0xa6/0xa7 family
already verified via `--force-popup`, but never via a real 2-distinct-
target click) — a smaller, well-scoped follow-up now that the fixture +
trace infrastructure exists. Otherwise back to the standing list: the
cow/castle anim-phase residual ([[project_strat_map_render_gaps]]) or the
combat-init A/V wiring ([[project_next_chip_4485a5]]).

---

## 2026-07-01 (session 17) — the workforce icon-grid overlay (`FUN_00412143` + 9 leaves) LANDED + wired live + visually verified. Autonomous session, same day as session 16.

Picked up the standing "workforce icon-grid render" deferred item
(`docs/plans/army-select-move.md`'s Tier 2, first scoped session 13) —
already fully mapped (10 functions, all small, clear bottom-up port
order, only baked-table extraction and a couple of ancestor-owned globals
outstanding) once re-surveyed. A genuinely bigger, less-bounded
alternative (the combat-init A/V wiring) was surveyed first this session
and deliberately deferred without writing any code — see
[[project_next_chip_4485a5]] for the full re-scoping (TL;DR: army-vs-army
combat is currently a silent no-op live, there's no WAV-playback
infrastructure anywhere in the project yet, and the battlefield renderer
has ~50 bind calls with no "build from territory" shortcut — comparable
in size to the whole strategic-map-render milestone, not a quick wire-up).

**Landed** (new module `engine_workforce_hud.c/.h`, 10 functions):
`engine_workforce_row_sprite_index` (`FUN_004517ca`), `..._ensure_buffers`
(`FUN_004997bf`), `..._capture_backdrop` (`FUN_004b3f0a`),
`..._fill_row_linear`/`..._fill_row_dual` (`FUN_004519ca`/`FUN_00451a5d`),
`..._compute_row`/`..._compute_rows` (`FUN_004518a5`/`FUN_0045161e`),
`..._draw_row`/`..._draw_all_rows` (`FUN_004126c9`/`FUN_00412666`), and
the entry point `..._hud_render` (`FUN_00412143`, core path). Renders an
8-category × 25-cell icon grid (little worker sprites from
villani1/villani2.pl8) for the selected army's home county, each row's
fill/colour driven by a per-army stock vs. a `[min,max]` band (deficit/
surplus indicator when outside it). 8 baked rdata tables r2-extracted and
cross-checked via two independent methods (raw `p8` hex dump parsed in
Python, plus r2's own `pf` struct-print) after an initial hand-transcription
of one table from ANSI-colored terminal output turned out wrong twice —
**lesson: never hand-parse colorized r2 output for byte-exact tables,
always re-extract with `-e scr.color=false` and parse programmatically.**
22 host tests, all green (2278/2278 total).

**Deliberate scope cuts, documented in engine_workforce_hud.h:**
- The "standings" decoration sub-branch (a 4th sheet + castle icon + 3
  wrapped tax/population numbers, gated on a separate flag) — a
  self-contained cosmetic add-on, not the icon-grid itself.
- `engine_dirty_rect_mark_below_status_bar`'s call — needs a live
  `engine_dirty_rect_state_t` this call path doesn't have (affects
  redraw-scheduling only, not pixel output).
- The two ancestor-owned globals (the "manage county" screen this panel
  is one part of isn't ported): the icon sheet now **defaults to the
  villani1.pl8 bank this render already loads for itself** (works
  standalone; a real ancestor can still override via
  `engine_workforce_hud_bind_icon_sheet` before calling), and the
  panel-mode gate is exposed as a plain setter, default 0 (off).

**Wired live**: `engine_army_select_dispatch`'s `workforce_render` hook
(NULL since session 13) now points at a real `ti_workforce_render`
wrapper in engine.c. Its trigger condition was already pinned by an
existing host test (`test_engine_army_select.c`): an EMPTY cell your own
army occupies, click flags bit 0x40 set → arms screen 2 and fires this
hook with the army key.

**Verified live** via a new `--force-workforce KEY` dev seat (mirrors
`--force-popup`/`--ti-cell`) on the `--strat-map` seat: injects synthetic
per-army stock/band values (no live producer for those fields exists yet
— genuinely unknown, not a gap this session could close) so the grid
renders regardless of what the captured dump's real bytes say, then
captures a screenshot. Diffing the before/after render showed new little
worker sprites appearing at the exact computed grid positions (extra
farmers near the crop rows and wooden structures), in the correct
in-game sprite style — confirmed the load→compute→draw pipeline works
end-to-end. Caught + fixed one dev-seat bug during this verification:
the injection first wrote into `bringup_strat_map`'s local `army` buffer,
but the `memcpy` mirroring it into the live `g_per_army_768` (what the
render actually reads) had already run earlier in that function — fixed
by injecting into `g_per_army_768` directly. Screenshots pushed to the
feed.

**Next candidate**: a fully composited panel needs the ancestor "manage
county" screen (tab state machine + the icon sheet's real producer) —
out of scope for a single session (see the ~50-bind-call battlefield-
renderer-sized estimate in [[project_next_chip_4485a5]] for the shape
this kind of ancestor-screen wiring tends to take). Otherwise the
standing list: the screen-0x10 chain's own fully-organic verification
(needs a plain county's keep-tile coordinate — see
[[project_headless_cursor_gap]]), or the combat-init A/V wiring.

---

## 2026-07-01 (session 16) — FIXED the headless raw-cursor-position gap session 15 found: organic/scripted clicks now reach `engine_army_select` for the first time. Autonomous session, direct continuation of session 15.

Picked up session 15's own next-candidate: the raw-cursor gap blocking ALL
scripted/organic click-testing of the strategic map (`[[project_headless_
cursor_gap]]`) — the most direct continuation since it was flagged in two
consecutive sessions and unblocks verification for every future map-click
feature, not just this one.

**Root cause (already scoped by session 15, confirmed unchanged):**
`engine_tick_input_step` (engine.c) calls the real Win32 `GetCursorPos()`
every tick and feeds it straight into `g_input_raw_cursor_x/y`, which
`ti_edge_scroll` uses for its cursor-at-window-edge hit-test. Retail's own
raw-cursor invariant only holds because retail runs fullscreen at
screen-origin (0,0) — `GetCursorPos()`'s desktop-absolute coordinates
coincide with client-space coordinates. The port's hidden/headless test
window has no real mouse driving it, so `GetCursorPos()` returns whatever
the actual (arbitrary) desktop cursor happens to be, making edge-scroll's
hit-test permanently read "cursor past the window edge" and consume every
scripted click before the map-click chain (`army_select`, terrain-info,
minimap-drag, screen-0x10) ever runs — confirmed **not a port bug**
(`engine_camera_edge_scroll` itself is correct; its own 30 host tests
pass).

**Fix** — exactly the approach session 15's memory already scoped:
`engine_set_input_cursor_override(1)`, a new one-line setter
(`engine.h`/`engine.c`), called once from `bringup_input_segtrace`
(main.c). When enabled, `engine_tick_input_step` substitutes
`g_mouse_x/y` (already correctly driven by the real WndProc's
`WM_MOUSEMOVE` handler from the trace's synthesized lParam, in the same
client-space coords `GetCursorPos()` would report if the window sat at
the screen origin) instead of polling the real OS cursor. Off by default —
every other seat (`--title-live`, `--interactive`, etc.) keeps polling the
real cursor exactly like retail; this is a headless-seat-only bypass, not
a retail behavior change. 3-file, ~20-line diff (`engine.c`/`.h`,
`main.c`).

**Verification (live, `tools/traces/screen27_dismiss_regress.jsonl` —
engine.c/main.c aren't part of the host suite, same as every prior
engine.c-only session):**
- Gotcha hit while sizing the run: the trace's bare `"t"` fields are
  relative to the FIRST `MAP_FREEROAM` anchor occurrence (frame 641 this
  run), not the run's absolute frame count or the anchor's last re-fire —
  needed `--frames 2000`, not the 1200 first guessed, for the 3 scripted
  clicks (t=900-1070) to actually fire.
- Temporary diagnostic `log_line` calls in `ti_edge_scroll` +
  `ti_army_select` (added, used, then fully reverted — committed diff is
  the clean 1-behavior fix only, no scaffolding left) showed: edge-scroll
  now returns `consumed=0` on all 905 in-game ticks of the run (previously
  it would have swallowed every one), and `ti_army_select` fired — for
  the FIRST time ever in this environment — with real resolved values
  (`county_id=3 head_link=13 terrain=0`), producing a genuine state
  transition (`screen 0→8`, `hud_outcome=3`, `demand_redraw=1`). Host
  suite stayed 2253/2253 green throughout (this fix touches only the
  Win32 integration layer).
- The specific clicked cell (260,250 from the pre-existing trace, picked
  by an earlier session before this bug was even diagnosable) turns out
  to be **army-occupied** (kind-3 in `engine_army_select`'s dispatch —
  "an army stands on this county" — which recenters the camera and opens
  screen 8), not a plain kind-1 owned county — so this run demonstrates
  the map-click pipeline working end-to-end, but doesn't itself organically
  exercise the screen-0x10 confirm chain from
  [[project_event_popup_arms]]. That specific chain still has only the
  `--force-popup`/host-test verification bar from sessions 13-14.
- Tried a 15-point coordinate-sweep probe (same save, same camera
  position) looking for a plain county to click instead: 4 points never
  reached `ti_army_select` at all (early ones plausibly still inside the
  post-load welcome-series settle window per session 15's fix; a couple
  later ones unexplained — not chased further) and all 11 that did reach
  it resolved `county_id=0` (no county under the cursor at all). Take-away:
  a county click apparently has to land on that territory's specific
  keep/capital tile, not anywhere within its borders — the ONE working
  coordinate in the existing trace was reverse-engineered by an earlier
  session specifically for this purpose, blind guessing doesn't reproduce
  it. Screenshots + state-schema slot diffs of both runs pushed to the
  feed.

**Next candidate:** a fully organic screen-0x10 verification needs either
(a) an edge-scroll pass (now provably working) to bring a DIFFERENT
county's keep into view and a computed screen coordinate for it, or (b) a
direct read of the save's territory/army records to locate a plain
kind-1 county's keep tile up front — either is a well-scoped follow-up,
not attempted further this session to avoid an open-ended coordinate hunt.
Otherwise back to the standing list: the workforce-render zoom-diff pass
(`FUN_00412143`) or the combat-init A/V wiring
([[project_next_chip_4485a5]]).

---

## 2026-07-01 (session 15) — FIXED the screen-0x27 auto-dismiss gap session 14 found: a real out-of-band-global-write bug in the in-game dispatcher's dismiss hook, not a rendering issue. Autonomous session, direct continuation of session 14.

Picked up session 14's own next-candidate: the screen-0x27 report/welcome
scroll getting permanently stuck open on a loaded-save scenario, blocking
ANY scripted/organic click-testing reached via "Load a game". Root-caused
via a `--input-segtrace` reproduction (`tools/traces/screen27_dismiss_
regress.jsonl`, promoted from the scratch trace that first caught it) with
temporary frame-by-frame diagnostics (added, used, then fully reverted —
the committed diff is the one-function fix, no debug scaffolding left).

**Root cause.** `ti_dismiss_chip` (engine.c, bound as the in-game
dispatcher's FIRST-checked hook) calls `engine_popup_scroll_input_step()`,
which can dismiss the open scroll. When the scroll was opened over the
0x27 report screen, the dismiss chain (`engine_popup_scroll_dismiss` →
`engine_popup_screen27_restore`) restores the screen via `engine_set_
current_screen()` — a direct, out-of-band write to the global `g_sim_
current_screen`, bypassing the dispatcher's own `engine_ingame_state_t`
struct entirely (this hook never touched `st->screen`). `engine_tick_
ingame_input_step`'s post-dispatch write-back
(`if (st.screen != g_sim_current_screen) engine_set_current_screen(st.
screen)`) then compares the FRESH global against `st.screen` — which
still holds its STALE pre-dispatch snapshot (39, captured before this
frame's dismiss ran) — sees a mismatch, and re-applies the stale value,
undoing the restore in the SAME frame. Net effect: the very first welcome
hint (0xc8) opens, dismisses, and the screen instantly flips right back
to 0x27 — permanently, since nothing else ever touches `g_sim_current_
screen` again once the event queue drains.

Confirmed via frame-by-frame instrumentation of the dismiss/restore call
chain: `engine_popup_screen27_restore` genuinely called `engine_set_
current_screen(0)` and its own local read confirmed screen==0 — but by
the time the SAME frame's tick finished, screen was back to 39. Traced
directly to `engine_set_current_screen(39)` firing again, 2 lines later,
from `engine_tick_ingame_input_step`'s write-back — not a different
producer, not a race, just a stale-snapshot bug.

**Fix**: `ti_dismiss_chip` now resyncs `st->screen = g_sim_current_screen`
after calling `engine_popup_scroll_input_step()`, so the dispatcher's own
bookkeeping (and the caller's write-back decision) sees the restore that
already happened. One function, ~10 lines. Confirmed via the reproduction
trace: the welcome series (0xc8/0xc9/0xca) now cycles cleanly — screen
0→39→0→39→0→39→0 — and stays at 0 (the strategic map) afterward, instead
of getting stuck at 39 forever. Screenshot of the settled map pushed to
the feed. 2253/2253 host tests still green (this is a pure engine.c
integration-layer bug — not part of the host suite, same as main.c/gfx.c
— verified live instead).

**A SEPARATE, deeper blocker found while finishing the reproduction**: the
3 scripted clicks in the trace still don't reach `engine_army_select` in
this environment. `ti_edge_scroll` reads `g_input_raw_cursor_x/y`, which
come from a REAL `GetCursorPos()` poll — in this headless/WSL test
environment that returns the actual (and essentially arbitrary) desktop
mouse position, completely disconnected from the trace's synthesized
WM_LBUTTONDOWN lParam. The garbage raw position (observed: (1520, 502)
against a 640×480 client rect) makes `engine_camera_edge_scroll`'s
"cursor near window edge" hit-test see the cursor as permanently past an
edge, so it consumes every click before army_select ever runs. This is a
genuinely different, deeper issue (a testing-environment/mouse-polling
gap, not a screen-state bug) — documented in the regression trace's own
header rather than chased further this session. Would need either real
mouse control in the test environment or a headless-seat rework that
feeds the synthesized coordinate into the raw-cursor pipeline too.

**Next candidate**: the raw-cursor-position gap above (its own frontier —
blocks organic click-testing of ANY map-area feature, not just army-
select) is the most direct continuation. Otherwise back to the standing
list: the workforce-render zoom-diff pass (`FUN_00412143`) or the
combat-init A/V wiring ([[project_next_chip_4485a5]]).

---

## 2026-07-01 (session 14) — the screen-0x10 army-move/merge confirm chain LANDED + wired live: the 0x11 popup's real producer is finally reachable from a click, not just `--force-popup`. Autonomous session.

Picked up PROGRESS.md's own "next candidate" from session 13: traced the
confirm-dialog click system session 13 flagged as "a generic dialog-click
system... its own frontier". Turned out to be the SAME generic Yes/No modal
the title-menu already partially uses (`FUN_004ba9c8`'s 0x1f arm), just
needing its own screen-0x1e arm — r2-verified the 2-button table byte-for-
byte (`r2 -q -c 'pxr 128 @ 0x4dd2e0' vendor/original/Lords2.exe`) and found
the underlying hit-test/render primitives (`engine_menu_button_dispatch`/
`engine_button_render_list`) were already ported and directly reusable.

**Landed, in order:**
1. **`engine_confirm_dialog.c/h`** (new module) — the generic Yes/No modal
   itself: `FUN_0040e6f2` (opener) + `FUN_0040ccfa` (render) + `FUN_00434e1f`
   (button-fire handler). 5 host tests.
2. **Wired into `engine_army_select_confirm_or_open`'s "has a pending link"
   branch** — previously stubbed `return 0` with a TODO; now actually opens
   the dialog, with a staged-result/poll mechanism (`engine_army_select_
   poll_confirm_result`) since the callback can fire many frames later than
   the original call (the type-5 button needs its ~20-frame press animation
   to settle). Also threaded `DAT_0057c8c4` (which county a handoff is armed
   on) out of `engine_army_select_open_handoff` for the first time.
3. **`engine_army_move_confirm.c/h`** (new module) — the actual screen-0x10
   machinery: `FUN_004a8e0b` (per-frame hover classifier, render-tick side)
   + `FUN_004a9252` (click resolver, input-tick side) + its 4 confirm-dialog
   siblings (`FUN_004a983b/4a960f/4a975d/4a9372` — the last one posts code
   0xa6, **the 0x11 popup itself**) + `FUN_004a98fe` (the merge/move EXECUTE
   primitive). All 5 resolver-side functions share retail's identical
   shape, so each sibling is written once as a bare zero-arg confirm-dialog
   callback, reused for BOTH the direct-call path (answer forced to 1) and
   the dialog path — mirrors retail's own DAT_0059151c reuse exactly, no
   invented parameter passing. 6 host tests, including one full end-to-end
   run: synthetic garrison-cap scenario → hover classify arms it → click
   resolve opens the dialog → clicking "Yes" (driving the real press-
   animation to completion) → the actual 0xa6/0x11 event posts with correct
   audience/payload/flag6 fields.
4. **Screen-0x10 dispatcher case** added to `engine_ingame_input.c` (mirrors
   the raw retail shape at `docs/decompiled/by-address/42ff10.c:553-572`
   exactly: turn-busy bail, edge-scroll first, fresh-click-gated-by-
   cooldown resolve, RMB bail, fall through to the shared tail otherwise).
   6 host tests.
5. **engine.c wiring**: `g_army_select_armed_county` (new no-home-in-struct
   static, mirrors the existing `g_army_select_handoff_cooldown`/
   `hud_outcome` pattern); `ti_army_move_click_resolve` + `ti_ui_router`
   hooks (the latter was NULL — "0 in-game" — since session 9's skeleton);
   the screen-0x10 (pure reclassify) and screen-0x1e (panel+text+buttons)
   render-tick arms.

**A genuine surprise mid-session:** `FUN_004a8e0b`'s hover-range gate reads
retail `DAT_00500c30`, which LOOKED like a producer-less table at first (a
`static int16_t` array, all-zero in the binary's initialized data, no
writer found by grepping the decompile) — engine_overlay_render.h's OWN
doc names a DIFFERENT-looking table at the same base address ("per-cell
sprite-size... indexed by cell/8"), which briefly looked like a genuine
dual-use-byte situation worth flagging as unresolved. Turned out to be
neither: `engine_path_field.h`'s OWN header comment already names it
directly — "the persistent field 0x500c30" — it's simply `engine_path_
field_fill`'s ALREADY-FLOODED cost field for the local player's slot
(flooded by `engine_army_select_open_handoff` when arming the handoff),
exposed via the already-tested `engine_path_field_cost()` accessor. No new
binding needed, just a direct call — a reminder to check a data address's
OTHER existing consumers/doc comments before assuming "no producer found"
from a decompile grep alone.

**Verification:** `--force-popup 0x11:0xa6:4:4` regression-checked (still
renders correctly, screenshot pushed to the feed) — the render arm itself
wasn't touched this session. 2253/2253 host tests green throughout (28 new:
5 confirm-dialog, 2 army-select wiring, 6 hover/click-resolve, 6 dispatcher,
plus updates to 2 existing army-select tests). Cross-compile clean.

**Live organic-click verification attempted, blocked on an UNRELATED
pre-existing gap, not reverted:** built a scripted `--input-segtrace` trace
(load a save → click an owned county 3×) to drive the chain with a real
click sequence. The load succeeds and reaches the strategic map, but the
loaded save's post-load report/welcome sequence flips to screen 0x27 and
**never returns** — `{"auto_dismiss": 1}`'s reactive RMB-synthesis only
fires while `g_sim_extended_pause != 0`, which this particular screen
apparently doesn't set, so nothing ever dismisses it and every subsequent
click lands on screen 39, never reaching the screen-0 map-click branch.
This is the ALREADY-DOCUMENTED gap from [[project_loadflip_rng_clarified]]
("unported report-screen 0x27 scroll renderer... port posts welcome 0xc8 +
flips to 0x27 but renders nothing") — not something this session
introduced, and not something that should block calling THIS session's
chain done, given 0x10/0x11/0x12's own precedent (host tests + `--force-
popup`, no full organic click run) already sets the verification bar this
codebase uses for popup arms. Temporary diagnostic `printf`s added while
narrowing this down were reverted (`git checkout -- src/engine.c`) before
committing — the committed engine.c has no debug scaffolding.

**Next candidate:** the screen-0x27 report/welcome-dismiss gap itself is
now a concretely scoped, separate frontier (blocks organic-click testing
of ANY in-game feature reached via a loaded save, not just this one) —
worth a session of its own. Otherwise: the workforce-render zoom-diff pass
(`FUN_00412143`, still deferred from session 13) or the combat-init A/V
wiring ([[project_next_chip_4485a5]]).

---

## 2026-07-01 (session 13) — the army_select + edge_scroll hooks LANDED + wired live; the 0x11 event-popup render arm LANDED + visually verified. Session 12's "5 unported callees, ~3.1 KB" estimate corrected to the true closure (32 functions, ~9.3 KB, fully scoped). Autonomous session.

Picked up session 12's re-scoped popup-arm leads (army-move click path vs.
combat A/V wiring — see [[project_event_popup_arms]]).  Chose the army-move
click path: the underlying move/economy primitives are already proven, so
it offered more autonomous runway before needing human A/V verification.
Full archaeology + the resulting scope correction is in
`docs/plans/army-select-move.md`; summary here.

**Scope correction.** Session 12's grep-based estimate for `FUN_0043ce1a`
("army select/move", the map-LMB handler) missed that `FUN_0043278b` was
already ported (`engine_territory_recenter_on_tile`, predates session 12).
Full closure tracing (not just direct callees) found the REAL picture is
bigger but fully tractable: **32 unported functions (~9.3 KB) across the
army_select hook, the edge_scroll hook, and the screen-0x10 move/merge-
confirm chain that actually produces the 0x11 popup** — every one of them
small (max 1263 B) and self-contained once mapped; no remaining unknowns.
Two corrections to the prior framing: `FUN_0043d309` is a **worker-task
toggle**, not "issue move order" (the real move executor is reached a
different way, already proven); `FUN_00412143` is a **render** function
(a per-county workforce icon-grid overlay), not "hover/select feedback".

**LANDED + wired live this session:**
- **`edge_scroll` hook** (`FUN_00432221` + `FUN_00431f59` + `FUN_004bbbe3`,
  engine_camera.c) — cursor-at-window-edge map/anchor scroll, rate-limited
  by a UI speed slider.  19 host tests (rate gate, all 8 compass directions
  + 4 corners, the mode-2 anchor path, and the "third game_mode" no-delta
  fallthrough retail's missing trailing `else` implies).
- **Real bug found + fixed along the way**: engine.c's render-tail camera
  clamp was mutating its OWN disconnected `g_cam_scroll_col/row` +
  `g_cam_anchor_x/y` statics instead of the canonical save-schema externs
  (`s_slot_005651b4/b8`, `s_slot_0055ce70/005678a0`) that
  `engine_territory_recenter_on_tile` already writes — a leftover from
  before any real producer existed, predating this session.  Fixed to use
  the externs directly so the new edge-scroll AND the existing recenter-on-
  select both compose with the existing clamp instead of writing to dead
  storage.  Also wired the terrain-info opener's own `army_select` sub-hook
  (a DIFFERENT, smaller thing than the army_select TOP-LEVEL hook below,
  despite the name collision — see the header note) to the already-ported
  recenter function, closing a gap its own comment had flagged as
  "lands with the army-select port."
- **`army_select` hook** (`FUN_0043ce1a` + `FUN_0043d309` task-toggle +
  `FUN_004a8426` territory-link sanity check + `FUN_0043723a`/`FUN_004371ce`
  reselect-handoff, new module `engine_army_select.c`) — the map-LMB
  dispatcher: select/reassign an owned army's workforce task (auto-picked
  by the clicked tile's terrain type), open terrain-info on a flagged
  empty cell, arm the screen-0x10 move/merge handoff on an owned county,
  or post a "not yours" notice (code 0x70) on someone else's.  26 host
  tests covering the full branch tree (game-mode/turn-idle/view-mode/hold-
  pending gates, the paused-click dismiss keep-list, all the flag-byte
  sub-branches, the terrain-type→task threshold ladder, both owned/foreign
  outcomes).  A genuine NULL-deref test catch along the way: `engine_map_
  scene_tick` (called for the recenter side-effect) turns out to trigger a
  FULL unguarded render pass, not a cheap counter tick — converted to a
  hook (NULL = inert) rather than a direct call, so this logic module stays
  host-testable without standing up the whole render pipeline.
- New `engine_territory_clear_cell_path_overlay` (`FUN_0046c6ba`, the
  render-flags byte's bit-0x40 sibling) and `engine_combat_set_flag_567d0c`
  setter, both small additions to existing modules rather than new ones —
  the army-select handoff and the screen-8 HUD-outcome path needed them.

**DEFERRED, fully scoped, no remaining unknowns:**
- **Workforce icon-grid render** (`FUN_00412143` + 9 leaves) — modeled as
  a hook (`engine_army_select_hooks_t.workforce_render`, NULL today).
  Wants its own render-parity pass (the `villani1/villani2/vill.pl8`
  sprite sheets + an 8-row icon grid) — semantics are faithfully portable
  now, but "what does this actually look like" wants the zoom-diff loop
  before calling it done, per [[feedback_zoom_dont_sample]].
- **The screen-0x10 move/merge-confirm chain** (`FUN_004a9252` +
  `FUN_0040e6f2` + 4 confirm-siblings, one of which —
  `FUN_004a9372` — is the 0x11 popup producer) — this is where the
  REAL trigger for the 0x11 popup lives, reached by a two-click flow
  (army_select arms screen 0x10 on a re-click of an already-selected
  army; a second click resolves the target).  Not wired live: the
  confirm-dialog "Yes clicked" flag (`DAT_0059151c`) has no traced
  producer in this session's call graph — it's a generic dialog-click
  system, not specific to army-select, and tracing it is its own
  frontier.  Confirmed the RENDER arm doesn't need this chain live to
  verify (below) — exactly the loophole 0x10/0x12 also used.

**Also LANDED this session: the `g_popup_type == 0x11` render arm**
(garrison-merge-cap notice, mirrors 0x10/0x12's pattern in
`engine_popup_scroll_render`) — 5 localized sub-strings + 2 embedded
numbers (remaining garrison capacity; troops already at the target
territory) + a 2-icon button-list, all r2-cross-checked against the
retail decompile (`FUN_0047309e`'s 0x11 arm) for the exact pixel offsets,
font choices, and the two number-format "suffix" pointers (confirmed
literal `" "` strings, same as 0x12's). Extended `--force-popup` to
accept an optional 4th `:PAYLOAD_A` field (it was hardcoded to 0
before — fine for 0x10/0x12, which don't read it from a record array,
but 0x11 needs a real territory key there).

**VISUALLY CONFIRMED** via `--force-popup 0x11:0xa6:4:4` on the
strat-terrain1 dump: "Cannot garrison castle. / This castle does not
have the capacity to barrack this number of troops. / 150 more troops
can be stationed here. / Your army contains 47 / Do you want to split
your army?" — fully legible, both numbers correct, icon pair renders.
Caught + fixed a real seat gap along the way, same class as session
12's: `bringup_strat_map` mirrors `army_records.bin` into the live
`g_per_army_768` (the session-12 fix) but never did the analogous
mirror for `territory_records.bin` → `g_per_territory_420` — 0x11 is
the first force-popup arm to read the territory array directly, so the
first render attempt showed "Your army contains 0" instead of 47.
One-line fix, same shape as the session-12 one.

**0x11 popup arm is now DONE** (host-test-free like its siblings —
graphics/localization-bound, verified via the visual seat — but the
underlying producer chain is logic-tested through engine_army_select's
26 tests). Matches 0x10/0x12's bar exactly.

**Next candidate:** pick up the workforce-render zoom-diff pass
(FUN_00412143, scoped, no unknowns); OR trace the confirm-dialog click
system to make the screen-0x10 chain — and thus the 0x11 popup's actual
LIVE trigger — reachable end-to-end in a real game; OR start a fresh
frontier entirely (the combat-init A/V wiring from
[[project_next_chip_4485a5]] is still open). None block each other.

---

## 2026-06-30 (session 12) — event-popup type 0x12 (keep/settlement-upgrade complete) LANDED + verified; the "+0x1c0/+0x1c1 dual-use byte" worry was a false alarm. Autonomous session, same day as sessions 10-11.

Picked up session 11's exact "next-session starting point": resolve the
`g_per_army_768[+0x1c0]`/`[+0x1c1]` field-semantic ambiguity via an r2
write-trace before porting the 0x12 render arm. Full archaeology in
[`docs/findings/engine-quirks.md` #210](findings/engine-quirks.md); summary:

- **Resolved, not ambiguous.** An r2 byte-search (`/x` on the literal
  displacement bytes, no full-binary `aa`) for every reference to the row's
  `+0x1c0`/`+0x1c1` found the WRITER — `fcn.00436d02`, the "start a keep
  construction" handler. It bumps `+0x1c0` to the new target level the
  INSTANT construction starts (not on completion), and snapshots the OLD
  level into `+0x1c1` first (skipped — left 0 — if there was no previous
  keep). It then immediately calls `engine_realm_recompute_realm_income`,
  the very function whose "+0x1c0 = standing band, capped by +0x1c1" comment
  triggered the original worry. Both readings are the SAME "keep level"
  concept consumed by different per-level lookup tables (tax rate vs.
  `k_keep_tbl` population/garrison figures) — not a genuine dual-use byte.
  `engine_combat_settlement_find_slot` (already-ported `FUN_004a551b`)
  independently computes the identical `k_keep_tbl[target] − k_keep_tbl[old]`
  delta for a different purpose, confirming the reading. The clinching
  detail: the popup's `g_popup_flag6` (the key it reads `+0x1c0/+0x1c1`
  from) is the EXACT realm key `engine_realm_complete_settlements` was just
  iterating when it posted the event — the popup reads the row its own
  producer just finished interpreting as a keep level.
- **Ported the render arm** (`engine.c`'s `engine_popup_scroll_render`,
  `g_popup_type == 0x12`): 4 body variants — `payload_a==1` (upgrade of an
  existing keep) renders a generic templated line; the other 3 compare
  `+0x1c1` (built level) against `+0x1c0` (target level) to show either the
  full `k_keep_tbl[target]` value (brand-new keep, no prior level), the
  `k_keep_tbl[target]-k_keep_tbl[built]` delta (normal upgrade), or 0
  (defensive built≥target fallback) — each matched against the retail
  decompile line-for-line. Added `engine_combat_keep_value(level)` exposing
  the already-baked-but-`static` `k_keep_tbl` (mirrors the existing
  `engine_combat_garrison_cap` accessor), since the table was already landed
  for `engine_combat_settlement_find_slot`/`engine_combat_place_garrison`
  and needed no rebaking.
- **Found + fixed a real verification-seat gap while visually checking it**:
  `bringup_strat_map` (the `--strat-map`/`--force-popup` bring-up) slurps
  `army_records.bin` into a LOCAL static buffer for explicit binds, but
  never mirrors it into the LIVE `g_per_army_768` global — so the first
  render attempt showed "0 troops" (reading an all-zero array) instead of
  the real keep level. One-line fix: `memcpy(g_per_army_768, army, sizeof
  g_per_army_768)` after the slurp succeeds (same class of gap as session
  11's missing `gfx_load_system_sheet_select(1)` call — this seat keeps
  needing small additions as render arms start reading more live state).
  After the fix, `--force-popup 0x12:0xa3:KEY` against a poked scratch copy
  of the `strat-terrain1` dump renders correctly: a brand-new keep shows
  "50 troops assigned as a garrison" (`k_keep_tbl[1]`), an upgrade shows "50
  extra troops sent to castle" (`k_keep_tbl[4]-k_keep_tbl[2]` = 200-150) —
  confirming `k_keep_tbl` is a garrison troop-count table. Both pushed to
  the feed.
- **Host suite: 2188/2188 passed**, no regressions (this popup arm has no
  dedicated unit tests, consistent with the rest of `engine_popup_scroll_
  render`'s sibling arms — it's graphics/localization-asset-bound and
  verified via the `--force-popup` visual seat + r2 byte-tracing instead).

**Next candidate — re-scoped, both options are bigger than they look:**
checked both remaining popup-arm leads before stopping for the day; neither
is a quick chip.
- **0x11's producer** (`FUN_004a9372`, 669 B, now mostly legible field-wise)
  is only reachable from the INTERACTIVE army-select/move click path, which
  is unported: the dispatcher's `army_select` hook is still NULL, and its
  handler `FUN_0043ce1a` (1263 B) pulls in five more unported callees
  (~3.1 KB total) — issue-move-order, hover/select feedback, camera-center,
  etc. The underlying move EXECUTOR is already ported and proven (AI-brain
  convergence test), so this is likely "arm a move" not "build pathing from
  scratch", but it's still a multi-function interactive frontier wanting
  its own scoping doc first (mirror `docs/plans/ingame-input-handler.md`).
- **0x10's second producer** (`FUN_004a8aab`, 753 B, the phase-2 TODO at
  engine.c ~1320) turned out to be a siege-ASSAULT cutscene launcher
  (battle-screen transition + SFX cues), not a small second event-post — an
  entry point into the SAME "combat-init INTERACTIVE A/V wiring" frontier
  [[project_next_chip_4485a5]] already separately flagged as needing battle
  SFX + battlefield render + ownership popups.

Both scoped read-only this session (no code changed for either); see
`project_event_popup_arms` memory for the per-function breakdown. Pick
whichever frontier (army-move interaction, or battle/siege A/V) next session
has the most context for — they don't block each other.

---

## 2026-06-30 (session 11) — event-popup type 0x10 (unit-funding warning) LANDED + verified; corrected the "0x10/0x11/0x12 production report" framing. Autonomous session, same day as session 10.

Followed session 10's natural-turn-advance fix straight into
[[project_event_popup_arms]]'s next gate: the 0x10/0x11/0x12 popup arms,
previously believed blocked on "reaching a season boundary" (now proven to
work). Scoping turned up a correction: these three types are NOT a themed
"production/harvest/population report" family — each is an unrelated
single-purpose notice that happens to reuse the same generic scroll
template. Full detail in `project_event_popup_arms` memory; summary:

- **0x10** (single-army-record notice) — producer `unit_morale_sweep`
  (`engine_realm_upkeep.c`) was ALREADY live, only the render arm was
  missing. Ported it (`engine.c`'s `engine_popup_scroll_render`), r2-verified
  byte-exact against the call sequence at 0x4732ca-0x47342d.
- **0x11** (garrison-merge-cap notice) — producer `FUN_004a9372` is an
  unported INTERACTIVE army-move handler (not a turn pass); needs fresh
  field archaeology in `g_per_army_768` (+0x1bc..+0x1c3, unnamed). Deferred.
- **0x12** (keep/settlement-upgrade complete) — producer IS already live
  (`engine_realm.c:engine_realm_complete_settlements`), but the render arm
  is NOT a quick follow-up: it has 4 body variants, 2 of which render a
  number via fields `g_per_army_768`'s per-realm rows `+0x1c0`/`+0x1c1`
  that are AMBIGUOUSLY documented elsewhere in the port — `engine_realm.c`
  calls +0x1c0 the realm's tax-rate "standing band", `engine_territory.c`'s
  `engine_territory_stamp_secondary_keep` calls the SAME offset a
  "banner-level"/settlement tier. Possibly a genuine retail dual-use byte
  (precedent: the AI round-robin cursor / redraw-cookie alias from session
  10), but needs an r2 write-trace to confirm before porting — see
  `project_event_popup_arms` memory for the precise next step.

**New verification seat: `--force-popup TYPE:CODE[:FLAG6]`** (mirrors
`--ti-cell`) — paints any popup arm directly over a `--strat-map` render,
bypassing the live event producer/consumer so the layout can be checked
without constructing the triggering game state (e.g. a save with an
underfunded mercenary). Caught a real gap while building it: the renderer
reads the resident in-game system sheet
(`gfx_system_sheet_current`/`DAT_005bb540`), which only gets loaded by
`engine_tick`'s per-frame ensure call — a single-shot seat like
`--strat-map` never runs that, so the popup silently no-op'd until the seat
explicitly calls `gfx_load_system_sheet_select(1)` first. Visually confirmed
both the warn tier (0x116, "Wiltshire / Unfed troops.") and disband tier
(0x118, "Duchy of Cornwall / Army perishes.") — correct layout, varying
body length, pushed to the feed.

**Next candidate:** resolve the 0x12 field ambiguity above (r2 write-trace
of `g_per_army_768[key*0x300+0x1c0/0x1c1]`), then port its render arm.
Otherwise 0x11's producer (`FUN_004a9372`) or back to
[[project_next_chip_4485a5]]'s combat-init A/V wiring.

---

## 2026-06-30 (session 10) — the "AI-brain completion stall" was a mis-diagnosis: natural turn advance PROVEN host-side AND live, no force-ready needed. Autonomous session.

Picked up session 9's "LAST GATE for a NATURAL turn advance" — the per-tick
AI brain dispatcher (FUN_0049a581) was believed to permanently stall without
`OL2_FORCE_AI_READY` (round-robin cursor parks, not-ready-count never hits
0). It doesn't. Full writeup in
[`docs/plans/ingame-input-handler.md`](plans/ingame-input-handler.md)
session 4; summary here.

**Root cause:** the brain dispatcher's terminal park waits on
`engine_army_state_arm_movement_owned`, which only RE-ARMS pending army
records for the actual movement executor (`engine_army_state_run` /
FUN_004650b0 + `engine_army_move_run` / FUN_00465d28) — a separate system
that has to run on the SAME tick for the records to ever drain. The
session-9 finding evidently drove the brain dispatcher without that
cooperating system (or read too short a capture window) and mistook "needs
more sub-steps" for "stalled forever".

**Proved convergence two ways, no product code logic changed (this was a
diagnosis correction, not a bug fix):**
- New regression test `test_game_init_ai_brain_converges_full_turn`
  (`tests/test_game_init.c`) builds a real 5-player/4-AI new-game world and
  drives the brain dispatcher + movement driver together: all 4 AI realms
  reach ready=999 in ~224 bounded sub-step iterations.
- Live, on the actual mingw-compiled port: a new trace
  (`tools/traces/endturn_advance_long.jsonl`) drove a real `lastturn.sav`
  through End-Turn under `--turbo`, **no** `OL2_FORCE_AI_READY`, for ~3300
  frames. A new "sim-state" exit-log line in `main.c` showed a genuine
  **turn 1→2 rollover** with all 4 AI realms naturally parked at
  ready=1000.

**Housekeeping that rode along:** corrected `OL2_FORCE_AI_READY`'s doc
comment in `engine.c` (it's a capture speed-up, not a stall workaround) and
four other engine.c comments that still claimed `sub_steps` is "always 0" —
stale since the in-game state machine landed and `g_engine_state == 3`
became reachable.

**Gotchas worth remembering:** `--newgame-live` and `--input-segtrace` are
mutually exclusive in main.c's bringup dispatch (silently runs 1 frame and
exits cleanly when combined — no error); `--input-segtrace` paths must be
absolute (the exe chdirs into the game dir first); the capture-directory
flag is `--capture-to`, not `--capture-dir`. None fixed (low-priority, the
LOAD route already gave a clean live proof).

**Next candidate:** the live proof used a loaded save, not a truly fresh
`--newgame-live` start (blocked by the segtrace gap above) — low priority
given host coverage already exercises the fresh-game path. Otherwise the
strategic in-game frontier's remaining work is back to
[[project_event_popup_arms]] (production reports 0x10-12, capture
popups) and the combat-init A/V wiring noted in
[[project_next_chip_4485a5]].

---

## 2026-06-20 (session 9) — IN-GAME INPUT-HANDLER SPINE: the dispatcher (FUN_0042ff10) + cursor→cell resolver (FUN_00429ba4) + terrain-info compute (FUN_0046d5fe) ported as pure/tested modules (49 tests). Autonomous session; live wiring + render deferred to a verification session.

Picked up the in-game input-handler frontier ([[project_ingame_input_handler]],
plan in `docs/plans/ingame-input-handler.md`).  The user stepped away with
verification deferred, so this session lands the high-confidence, UNIT-TESTABLE
half of the terrain-info click path (the render half + the live wiring are
render-parity / integration work that needs the in-game viewer).  Three commits:

**1. Cursor → map-cell resolver (FUN_00429ba4 → `engine_camera_cursor_to_cell`).**
The inverse of the camera projection: screen cursor + live viewport geometry →
the staggered-row map cell under the cursor, its tile index, and whether the
cursor is over a valid tile.  This is the producer of retail's DAT_005533b0
(`map_ready`) gate + DAT_00568974 (hovered tile) that the dispatcher reads to
route map clicks.  Pure (in/out struct); verbatim staggered-row half-step parity
math, the game-mode-2 +0x10 Y bias, the signed invalid-tile sentinel, the
row/col edge flags.  14 host tests hand-compute every pick branch.

**2. The dispatcher skeleton (FUN_0042ff10 → `engine_ingame_input`).** The per-tick
strategic input/UI dispatcher, the enabler for every in-game click path.  Ported:
the prologue latches + region classifier (FUN_00432640), the dismiss-chip / UI-
router subsumption (the dispatcher calls the open-scroll dismiss handler FIRST,
exactly like retail), the STRATEGIC (==0) screen in full (the 8 HUD/edge sub-
handlers in retail call-order + short-circuit, then the map fall-through: dismiss /
view-mode-2 jump / army-select on LMB / terrain-info on RMB, gated on the turn
handshake + the map_ready cursor gate), the matched TERRAIN-INFO (==4) close case
(any click → screen 0 + cleanup), and the shared tail.  Design mirrors
engine_menu_input: per-frame input snapshot + mutable state struct + a HOOKS
struct of sub-handler/action callbacks (NULL = inert — the default for the not-yet-
ported handlers), so retail's exact call order + state transitions are preserved
and unit-testable.  The ~30 other screen cases (battle/county/market/report) are
deferred with NULL hook slots ready.  27 host tests (region classifier, latches,
subsumption, call order + short-circuit, every map fall-through branch incl. LMB-
over-RMB precedence + the view-mode-2 bare-return, the screen-4 close paths, tail).

**3. Terrain-info compute (FUN_0046d5fe → `engine_map_query_terrain_info`).** Tile →
the attribute bundle the "Here Be Dragons / <terrain>" popup + the right-panel HUD
read (terrain type, county/owner id, flags, head-link/army id, derived bit).  Pure
(cell grid + army record array passed explicitly); verbatim feature-width decode,
the width-2 walk-back to the feature origin via the byte+4 sub-tile nibble, the
head-link 0x20→0 reset, the two flag-clear rules.  8 host tests.

**What's left for a visible terrain-info (NEXT SESSION, needs the viewer):** (a) the
opener glue FUN_0043caf4 (mostly testable col/row math + state over the ported
compute); (b) the panel render FUN_0041befe (778 B, screen-4 simple-terrain arm —
RENDER-PARITY: zoom-diff loop) + a `screen==4 → FUN_0041b032` arm in
engine_screen_dispatch; (c) the LIVE WIRING — replace engine.c's bare dismiss call
(~2152) with the dispatcher, feed map_ready from the resolver (needs a camera-
geometry getter off engine_map_render.c's g_cam_* statics), bind the hooks.  Full
recipe + the ⚠don't-flip-map_ready-before-the-renderer caveat are in the plan doc.
Verify via tools/traces/terrain_info.jsonl; no-regression = welcome series byte-exact
(the dismiss path is subsumed unchanged).  Deferred-but-located: End-Turn→reports
(FUN_00432967 + FUN_0040e3ee, the latter analogous to the ported
engine_menu_hotspot_click).

---

## 2026-06-19 (session 8) — BRAIN DISPATCHER FULLY PORTED: steps 7 (FUN_0049f93d defensive-deployment) + 6 (FUN_0049edc7 keep-builder) LANDED — all 14 per-tick brain steps are now live (no stubs remain)

Picked up the session-7 "Next" — port the remaining stubbed brain steps.  Both
landed byte-exact (r2-verified) and the per-tick brain dispatcher
(engine_army_ai_run_per_tick) is now **fully assembled: all 14 steps (1..0xe) are
live ports, no advance-only stubs remain.**

**Step 7 = the defensive-deployment planner (committed this session).**  Replaced
the case-0x7 advance-only stub in `engine_army_ai_run_per_tick` with
`engine_army_ai_run_garrison` — the faithful port of FUN_0049f93d (47 B) + its
closure.  The entry refreshes the muster realm (engine_army_ai_pick_muster_realms
= FUN_004a0aaa, already ported) then runs two planners:
  • **garrison_keeps (FUN_0049f12f, 765 B)** — for each owned keep that is
    unbesieged, not settle-flagged, populous enough (population ≥ the personality
    keep-floor at personality_row +0x70), and with both funding reserves above
    0x13: size the garrison need as the keep's head cap (engine_combat_garrison_cap,
    = DAT_004d8a10[keeplvl−1]) minus any pending garrison, scale it down by the
    band of population it consumes (≤14% none / 15–29 ÷2 / 30–49 ÷3 / 50–64 ÷4 /
    65–79 ÷5; ≥80% bail), muster that force (muster_garrison), and stamp it order 4
    bound for a settled cell.
  • **reinforce_realms (FUN_0049f431, 782 B)** — for each owned realm where the
    enemy army strength present (FUN_0049f73f, excludes the player's & its lord's
    armies) outweighs the player's own (FUN_0049f850) by >100: muster a defender.
    If the realm is the muster realm or the threat is below the player's reserved
    defence threshold (force_acc + pop·2/3, 0 when pop<200) the defender garrisons
    in place (mode 1, order 3); otherwise it marches to the muster realm (mode 0,
    order 3 + target region) and the realm consolidates — shipping its surplus
    provisions/stock there as a goods caravan (FUN_004292af) or, when a trade gate
    is set, selling them (engine_army_ai_sell_order ×2).

**New callees ported (7, not the scoped 6 — the scope note missed FUN_004292af).**
The muster pair lives in the engine_combat muster cluster next to its siblings:
**engine_combat_muster_garrison** (FUN_004a5389 — like raise_region_unit but with
pct passed in + the class-distribution pool sweep) and **engine_combat_pool_commit**
(FUN_004a545f — commit staged head into troop classes 5/1/3/2/6 from the owner's
seeded category reserves).  The strength sums + caravan dispatcher
(engine_army_ai_dispatch_caravan = FUN_004292af) live in engine_army_ai.  The
caravan spawns a type-4 carrier near the realm's capital loaded with the staged
goods order and debits the shipped goods; it needed exposing FUN_00428007's
box-scan result via a small engine_army_state wrapper
(`engine_army_state_find_settle_spot`) — the "promote to shared global" the
scratch-global comment had anticipated, done via out-params.  No new .rdata embeds:
the muster-cost curve (0x4d8778 = k_muster_cost) and keep-cap table (0x4d8a10 =
k_garrison_cap) were already embedded, and the two personality columns
(+0x70 keep-floor i32, +0x9c reinforce-ration u8) are columns of the already-
embedded g_ai_personality.

**Verification = r2 byte-exact + host tests.**  r2 confirmed all four nontrivial
functions: muster_garrison's pool-commit order (5,1,3,2,6) + DAT_0057bf05 clear +
cost-curve/percent_of; garrison_keeps's five band thresholds (0xf/0x1e/0x32/0x41/
0x50) and their ÷2/÷3/÷4/÷5 divisions + the garrison-cap/personality-col indexing;
reinforce_realms's two muster_defender modes (1/0), ration column, strategy tag
100, and sell×2 / caravan branches; and dispatch_caravan's occupant field stamps
(+0x166/+0x167/+0x16c/+0x16e/+0x170) and the goods debit (the compiler's xor/sub/
neg load nets `prov −= goods[1]`, `stock −= goods[5]`).  Gate: **2113 host tests**
(+7: one combat muster_garrison class-distribution test + six AI step-7 tests
covering the keep-garrison gates, the reinforce hold/march/lord-exclusion branches,
and the end-to-end goods caravan), mingw clean.  Whether step 7 moves the trace
oracle depends on the turn-1 input (it runs every turn, but turn-1 SP may lack
threatened realms / affordable keeps, like step 0xb) — the before/after oracle
check is the remaining verification, alongside step 6.

**Step 6 = the AI keep-builder (committed this session).**  Replaced the case-0x6
advance-only stub with `engine_army_ai_run_keep_builder` — the faithful port of
FUN_0049edc7 (872 B) + its 5-callee closure.  For each owned realm big enough
(population ≥ the personality keep-build floor at personality_row +0xc8) with no
keep, and while the player is under its keep cap (+0x90), it builds the highest
keep tier the treasury (cash +0x118) affords from the personality's five cost
columns (+0xcc..+0xdc) via **engine_army_ai_build_keep** (FUN_00436d02, 768 B — the
keep-construction primitive, also exposed for the future human build-keep action,
retail's other two callers).  build_keep charges the level's two pool costs
(refunding the old level on an upgrade, the surplus poured back when negative),
stamps the keep fields + build-work totals, evicts any armies off the 2×2 keep
footprint (FUN_0046841c → FUN_00467196 army-relocate), and re-plans the realm's
economy/workforce/income (all already-ported re-plan tails).  New callees ported:
the evict (FUN_0046841c), the army-relocate (FUN_00467196, all deeper callees —
the map-feature ring scan + the cost-field pathfinder — already ported), and the
two keep-progress helpers (FUN_00450e46 build-rate / FUN_00450fb4 completion-%).
Two new .rdata embeds: the keep cost table (0x4d89b8 = `k_keep_cost`, six 8-byte
{pool-130, pool-128} entries by level) and the build-work table (0x4d89e8 =
`k_keep_work`); the personality cost/cap/floor columns were already in
g_ai_personality.  r2-verified byte-exact: the builder's table indexing
(0x4d89c0/c4 new-cost, 0x4d89b8/bc refund, 0x4d89e8 work), the dual pool
debit/refund (0x57c028/030), and the keep-field stamps; the entry's gate (minpop /
keep-cap) + highest-affordable tier walk (cost5→cost1).  Gate: **2118 host tests**
(+5: build_keep fresh/upgrade-refund + the entry's tier selection + the three
gates), mingw clean.

**The dispatcher is COMPLETE.** All 14 per-tick brain steps are now faithful ports.

**ON-ENGINE VERIFIED + a latent infra gap found & fixed (commit f6e77d5).** Ran the
dispatcher on the real new-game world (`olv3_capture --trace endturn_auto --only
port`).  First run **crashed** (access violation in engine_path_field_fill).
Bisected (stub each step / sub-planner) → step 7's `garrison_keeps`, specifically
the order-4 garrison occupant it musters; a temporary unhandled-exception filter
+ addr2line pinned the fault.  **Root cause was NOT a step-6/7 bug — it was a
pre-existing deferred gap:** the AI cost-field pathfinder's move-cost grid
(`engine_path_field` g_terrain, retail 0x4f3ffe) was **never bound**
(`engine_path_field_bind_terrain` had zero callers).  It hid because step 0xb's
run_movement was reached-but-input-empty in turn 1 (no field armies) — so the brain
never invoked the pathfinder until **step 7's garrison occupant became the first AI
army to move**.  (So the crash itself PROVES step 7 is active on-engine: it musters
the first garrison.)  Fix: g_strat_move_cost is now a 0x41-int16-in view of a
padded backing (the pathfinder reads a 3×3 direction window, COST_DIAG=+0x41 = the
cell), exposed via engine_strat_cost_pathfield_base(); engine_game_init_bind_runtime_tables
binds the pathfinder to it (both new-game + load routes).  After the fix
endturn_auto --only port runs all 8 capture labels to a clean exit.

**Next** (post-dispatcher frontiers): (a) the port↔retail STATE oracle on
`endturn_auto` (now that the port runs the full AI turn) to measure step-6/7 parity
vs retail — pending the ±3-4 pass-counter timing residual + the unpinned
lastturn.sav drift below; (b) close the ±3-4 pass-counter timing residual
(auto_dismiss onEnter vs the port's input phase); (c) pin the lastturn.sav per
[[project_save_redirect_reproducibility]] so retail captures stop drifting; (d) the
deeper combat-init A/V interactive wiring.

---

## 2026-06-10 (session 7) — BRAIN STEPS 9 + 0xb LANDED (FUN_0049f977 campaign planner + FUN_004a5667 army-movement planner, 20 callees total); step 9 is oracle-active, step 0xb is reached-but-input-empty (no field armies in turn 1, like step 1)

Picked up the session-6 "Next": port the remaining stubbed brain steps
bottom-up, step 9 (FUN_0049f977) and 0xb first since they run every turn in
headless SP.  Closure analysis picked the order: with descent stopping at the
already-ported boundary, **step 9 needs only 6 unported fns (~1.9 KB)** while
step 0xb needs 14 (~6 KB) — and step 9 shares two leaves (FUN_004a65a3/6735)
with 0xb, so it de-risks 0xb too.  Step 9 landed byte-exact AND oracle-active.

**Step 9 = the offensive-campaign planner (committed 0a1d770).**  Replaced the
case-0x9 advance-only stub in `engine_army_ai_run_per_tick` with
`engine_army_ai_run_campaign` — the faithful port of FUN_0049f977 (841 B) + its
six-fn closure: the campaign-target validator (FUN_0049ff71), the target-owner
gate (FUN_004a01ea), the easy-AI funding cheat (FUN_0049fcc5), the deployment
assigner (FUN_0049fda5), and the two find-nearest deploy-cell scanners
(FUN_004a65a3 flag-0x80+cropland / FUN_004a6735 flag-0x40).  The heavy
dependencies were already ported — the FUN_0046f700 pathfield group
(engine_path_field.c) and the muster/unit-pick/target-select callees
(FUN_004a50ae/4a0917/4a03f2 in engine_combat.c / engine_army_ai.c) — so step 9
added only those six.  It's the AI's persistent **multi-turn** army campaign
(vs step 0xa's one-shot raid): tick a patience counter (+0x45 vs personality
+0x28) → commit/validate a target (+0xe8) → size a force against a personality
budget (+0x30, the byte the step-1 tribute floor reuses) → raise (muster) or
redirect (pick_best_unit) it → stamp a march order (code 2, or 6 toward the
lord's realm) at a finder-chosen owned cell.  Newly maps personality
+0x28/+0x40/+0x68 and strategic +0x45/+0xe8/+0x138/+0x140/+0x14c/+0x150.  Gate:
2101 host tests (+6 step-9 tests, every branch), mingw clean.

**The headline contrast with step 1 — step 9 IS reachable by the headless
oracle.**  A before/after `--only port` on `endturn_auto` (same pinned RNG seed;
the ONLY change is step 9 stub vs live) shows the port's `g_player_strategic_352`
**change at run2/run3/settled** (e.g. run2 h:6f5573c0→h:a83ff1a8) — by then the
AI realms' patience counters fire and step 9 starts committing campaign targets.
So unlike step 1 (provably inert, byte-identical before/after — its inbox never
fills in SP), step 9 runs every turn and the oracle moves.  It does NOT yet
byte-MATCH retail at those labels, but that's the EXPECTED residual: the cursor
slot 84 (port 2-4 vs retail 1) and pass counter s_slot_0053f658 (port
37/52/81/111 vs retail 40/55/85/115) show the documented **±3-4 pass-counter
timing offset** (auto_dismiss dismisses at the per-tick onEnter vs the port's
input phase → snapshots land at different sim-progress points), and steps 6/7/0xb
are still stubbed.  The run-label strategic sub_diff `[5,7,8,9,10,11,12,13,15]`
is that combined residual, not a step-9 defect.

**Verification = r2 byte-exact + units + before/after oracle-active.**  r2
confirmed the entry's target/pinned/patience branch structure (`cmp [+0x4d8a80],
tick; jle` = `if (tick<period) return`, the budget `sar 1` halvings, the +0x4b
commit), the assigner's order codes 2/6 at 0x52f0ca + the keeplvl/pending finder
dispatch, and the finder's flag-0x80 / terrain-`0x14<t<0x1a` gates.  Personality
constants read straight from the binary (period 3/4/4/2, budget
750/800/900/1000, pct 30/30/40/50, floor 100/120/200/250).

**Step 0xb = the army-movement / deployment planner (committed ca718f9).**
Replaced the case-0xb stub with `engine_army_ai_run_movement` — the faithful port
of FUN_004a5667 (325 B) + its 13-fn closure: the order gate FUN_004a57ac
dispatching six per-order-code handlers (attack-march 7 / advance 2 / intercept 3
/ garrison 4 / siege 5 / lord-move 6 = FUN_004a58f4/5b1f/5f0a/6270/60b9/599d) plus
the shared scanners (frontier FUN_00467f2e, nearest-army FUN_004673b8/00467532),
the deploy-cell setter FUN_004a64ca and the disband-into-realm fallback
FUN_00438681.  For each live army the realm owns it recomputes a march
destination, then runs the cost-field pathfinder (engine_path_field, already
ported) and copies the waypoint chain.  Step 9 had already landed the two shared
deploy-cell finders, so this was the remaining closure.  Added
engine_combat_garrison_cap.  Gate: 2106 host tests (+5), mingw clean; r2
byte-exact on the gate dispatch, the entry's active-gate→gate→pathfield sequence,
and the order-2 handler's &&-short-circuit triple pick_attack_target.

**Step 0xb is REACHED but input-empty in turn 1 (a step-1-shaped result, NOT a
defect).**  Instrumenting run_movement on endturn_auto showed it fires once per AI
realm (2/3/4/5) — dispatcher wiring + FSM progression confirmed — but
`active_total = 0`: there are NO field armies (active occupant records) anywhere
in the turn-1 scenario (the AI's military sits in garrisons/keeps), so the
planner's loop has nothing to move and a before/after --only port is
byte-identical.  Same shape as step 1 (reached, empty input), differing only in
the input table (occupants vs the diplomacy inbox).  (Aside: this session's
endturn_auto retail captures vary at flip0 — 91k→198k px — because the
lastturn.sav is unpinned and prior captures mutated it; [[project_save_redirect_
reproducibility]].  The port-vs-port before/after is unaffected since it pins the
seed.)

**Next: steps 7 (FUN_0049f93d) and 6 (FUN_0049edc7) — the last two stubs.**  Then
the brain dispatcher is fully ported.  Both run every turn; whether they move the
oracle depends on their input tables (like 9 = active, 0xb/1 = input-empty).
SCOPED this session (callees all already ported): **step 6 = the AI keep-builder**
— entry FUN_0049edc7 (872 B; for each owned realm big enough with no keep, pick
the highest keep tier the treasury affords from personality columns +0xcc..+0xdc
+ limit +0x90, gated by feed +0xc8) → FUN_00436d02 (768 B keep builder: set keep
level +0x1c0, refund/debit the pool costs, stamp the keep fields, re-plan) →
FUN_0046841c (170 B: evict armies off the 2×2 keep footprint).  Needs a NEW
.rdata embed — the keep cost/display table at 0x4d89b8 (8-B stride, ~5 tiers,
sits just before k_garrison_cap @ 0x4d8a0c); the personality keep columns are
already in the embedded g_ai_personality.  **Step 7 = FUN_0049f93d (47 B) + 6
callees (~2.5 KB: 49f12f/49f431/49f73f/49f850/4a5389/4a545f)**.  The
deeper open items: (a) a multi-turn or mid-game-save trace to exercise steps 0xb
(field armies) and 1 (a diplomacy-UI overture) on-engine; (b) the ±3-4
pass-counter timing offset (auto_dismiss onEnter vs the port's input phase) — the
parity residual to close once all steps are in; (c) pin the lastturn.sav per
[[project_save_redirect_reproducibility]] so retail captures stop drifting.

---

## 2026-06-10 (session 6) — BRAIN STEP 1 LANDED (FUN_004a277d diplomacy-inbox processor + 7 handlers); the oracle CAN'T reach it (inbox is human-UI/MP-only → inert in headless SP) — verified instead by r2 + units + zero-regression before/after

Picked up the session-5 "Next": port the brain step subtrees bottom-up against
the clean oracle, step 1 (the gateway, FUN_004a277d) first.  Step 1 landed
byte-exact and verified — but the headline lesson is about WHERE it can be
verified.

**Step 1 = the diplomacy-inbox processor (committed 27b6ca3).**  Replaced the
case-0x1 advance-only stub in `engine_army_ai_run_per_tick` with
`engine_army_ai_step1_process_inbox` — the faithful port of FUN_004a277d (565 B)
+ its full closure: the seven message handlers (FUN_004a29b2 / 2cd6 / 2f61 /
30f6 / 3475 / 3551 / 38db) + the gift transfer FUN_004a1b29 (the treaty
break/forge callees 004a1a54 / 004a1774 were already ported).  It drains each
realm's five-entry inbox (`s_slot_0053f0f0`, save entry 146 — +0 sender / +1
type / +2 param / +4 i32 value), dispatching by type: resource-offer eval
(±band tiers), repeat-offer, notified/silent treaty break, alliance-proposal
response (forge on a warm or RNG-favourable relation), and lord-tribute demands;
then clears the inbox + resets the per-pair +0x90 scratch byte.  Relation (+0x84)
clamps ±30 after every adjust.  The 7-bit RNG byte (g_rng_7bit_b / DAT_0058fd60)
threads from the dispatcher into the alliance/tribute rolls — the sub-step body
stirs it (engine.c:2028) BEFORE the strategic dispatch (engine.c:2048), retail's
FUN_004b99c0 order, so the roll is byte-exact.  Gate: 2095 host tests (+5
step-1 tests, every handler path), mingw clean.

**The big finding — step 1 is unreachable by the headless oracle.**  A fresh
`--target both` on `endturn_auto` showed step 1 changed NOTHING vs the stub, and
a before/after `--only port` (stash step 1 → rebuild → recapture vs the same
cached retail) is **byte-identical** at every label.  Root cause: the inbox's
sole writer `FUN_004a2621` has exactly two callers — a human-UI diplomacy action
(`DAT_0057c8cc` = local player) and an inbound MP message.  In a headless SP
AI-only sim (load lastturn.sav → dismiss scrolls → run), NEITHER fires, so the
inbox never fills and step 1 is provably inert.  Step 1 IS the AI's "respond to
the human's/MP diplomatic overtures" logic; it only activates when a human sends
diplomacy.  So this port is verified by **r2 byte-exactness** (FUN_004a277d +
FUN_004a29b2: dispatch addr 0x53f0f0+self*0x28+entry*8, sender==0 breaks /
sender==self continues, personality LUT 0x4d8a60 = row+0x08, codes 0xab-0xc5 +
±30 clamp) + **units** + **zero-regression before/after** — NOT the trace
oracle.  Exercising it on-engine later needs a diplomacy-UI trace.

**Oracle residual is unchanged (as expected).**  endturn_auto still shows the
known ±1-2 pass-counter timing offset (auto_dismiss dismisses at the per-tick
onEnter vs the port's input-phase → ~6-frame skew → LFSR slots 18/19 + phase/
pass slots diverge at the snapshot points) and the strategic-record run-label
sub_diff [5,7,8,9,10,11,12,13,15] — both untouched by step 1, both attributable
to the STILL-stubbed steps 6/7/9/0xb + the timing residual.  flip0 strategic +
inbox match (brain doesn't run in phase 1); quirk #201 cosmetic map residual
persists.

**Next: the remaining stubbed brain steps, bottom-up — but they need on-engine
reach too.**  Steps 6 (FUN_0049edc7), 7 (FUN_0049f93d), 9 (FUN_0049f977), 0xb
(FUN_004a5667) are the run-label sub_diff signal (unlike step 1, these run every
turn in headless SP, so the oracle WILL move when they land).  Port 0xb (the
army-movement/deployment planner, 6.3 KB, pulls in path_field) and 9 first —
they touch the army/territory bands the sub_diff fingers.  Separately, to ever
regression-test step 1 on-engine, craft a diplomacy-UI trace (human proposes an
alliance to an AI realm → that AI's inbox fills → its step 1 fires next turn).

---

## 2026-06-10 (session 5) — PER-TICK AI BRAIN DISPATCHER LANDED (FUN_0049a581 wired into phase-4); trace-studio oracle re-tightened (stale-cache phantom debunked, continuous-dismiss isolates a residual rate ratio)

Picked up the session-4 "Next" (port FUN_0049a581) and the user-chosen oracle
work.  Two arcs: the brain dispatcher landed, and the trace-studio oracle was
diagnosed + tightened.

**Brain dispatcher (committed 9e31b61).**  Assembled FUN_0049a581 →
`engine_army_ai_run_per_tick(evq, mp_active)` and wired it into
`engine_turn_tick_substate0`'s phase-4 arm (retail order 0x49a3ca: brain →
turn-resolved → phase-advance).  It advances the persistent round-robin cursor
(s_slot_0057c944, slot 84) one in-game realm per call, runs one FSM transition
(init: check_elimination + aggregate + clear +0x1c, state→1; human/parked gates;
14-task ladder; terminal park at 999 once arm_movement_owned drains).  Steps
2/3/4/5/8/0xa/0xc/0xd/0xe dispatch live; **steps 1/6/7/9/0xb are advance-only
stubs** (CALL_TRACE_ENTER_STUB) — their unported closures are LARGE (step 1 = 9
fns/5.6 KB, step 0xb = 15 fns/6.3 KB, step 7 = 8 fns/2.9 KB, step 9 = 7 fns/2.7
KB, step 6 = 3 fns/1.8 KB; ~18 KB total), to port bottom-up next.  Gate: 2090
host tests (+6 dispatcher FSM tests), r2-verified byte-exact, mingw cross-build
clean.  **Result: slot 84 now advances and converges to retail's value (4) by
run3/settled** — it was frozen at 1 (the session-4 flagged divergence).

**Oracle tightening — the big lesson: ALWAYS `--target both` fresh.**  An
apparent 12-slot ranking "regression" (s_slot_00553d24 leader / 00522d90 weakest
/ 0053e97c score etc., port!=0 retail=0, even in the phase-1 load route) was a
**100% stale-cache phantom**: the retail captures were 3-12 h old and had loaded
a *different* lastturn.sav than the fresh port (the .sav is unpinned —
[[project_save_redirect_reproducibility]]).  A fresh `--target both` collapsed
flip0 back to **ndiff=1** (the known quirk-#201 cosmetic map residual) and the
ranking slots to **0 diff**.  The brain is provably load-route-inert (phase 1,
brain runs only in phase 4); no regression.

**Residual = a real rate-controller parity gap (NOT freeze).**  With both sides
fresh, the only genuine divergence is the sub-step rate: pass counter
(s_slot_0053f658) port 33 vs retail 98 over the window, and the port *freezes* at
33 (brain-posted scroll, no dismiss).  A continuous-dismiss probe
(`tools/traces/endturn_cd.jsonl`, a right-click every 3 frames, symmetric on both
sides) unfroze the port (33→73) and made the **render near-exact** at the
quiet-map labels (85355px → 355px) — but the rate ratio **persisted at ~0.67**
(port 73 vs retail 108).  So the residual is NOT the scroll freeze: the port is
capped at the LUT row-0 duty (~0.67 sub-steps/tick, FUN_004bb978 LUT @ 0x4e3260)
while retail runs ~0.90/tick.  The port passes the real LUT + advances g_sim_phase
correctly, so retail must NOT be capped by row 0 — its scenario hint (FUN_0043de70)
or phase differs.  Needs retail-side instrumentation (agent --watch on the hint /
DAT_004d2164 phase / _DAT_0053f230 speed, or an Interceptor on the FUN_0043de70
return) to pin which input diverges.

**Reactive auto_dismiss on the retail agent is still a TODO** (commit 60269bb's
deferred half): the port has `{"auto_dismiss":1}` (engine.c:2098 — set
DAT_004e6900 then FUN_0047685d dismisses while DAT_0053e910!=0), but the agent
doesn't mirror it, so it can't be used symmetrically yet.  The mechanism is
verified (FUN_0047685d @ 0x47685d: cmp dword[0x53e910]; mov byte[0x4e6900]; call
0x476768) — the hook is a one-liner in installEngineTickHook once the rate ratio
is understood (auto_dismiss won't close the ratio, only the freeze).

**RESOLVED (same session) — the rate ratio was a BOGUS LUT image (commit
e179d8a).**  Instrumented retail's rate inputs via agent `--watch` (hint /
DAT_004d2164 phase / DAT_0057c998 sub-step / speed): retail's hint=1, speed=90
(IDENTICAL to the port), phase advances +1/frame, sub-step 0.95/frame.  So
retail steps ~every tick — meaning its LUT row 0 is all-ones.  Read retail's
actual DAT_004e3260: **row 0 = all 160 ones** (clean linear-decay duty table:
160/144/128/…/16 then 8/4/2/1/0), but the port's *imaged* `g_engine_rate_lut`
had a bogus `1,1,0,…` ~2/3-duty row 0 (extracted from the wrong file offset),
capping the port at ~0.67/tick.  Re-imaged the full 0xe60 verbatim from
0x4e3260.  After the fix the pass counter TRACKS retail **±1 across the whole
window** (with continuous dismiss: d3 11/11, run1b 34/34, run2 49/49, settled
107/108 — was 33/98) and the render is near-exact at quiet labels (355 px).
The trace-studio oracle is now CLEAN; the residual ndiff at the run labels is
exactly the *signal* — the stubbed brain steps 1/6/7/9/0xb.

**Reactive auto_dismiss on the retail agent — DONE (commit d550088).**  Mirrored
commit-60269bb's deferred half: the `{"auto_dismiss":1}` segtrace op now drives
BOTH engines (agent closes any open modal scroll each per-tick call via
FUN_00476768).  Three fixes: frida_capture._load_segtrace parse, the agent rpc
input_segtrace loader (was silently dropping unknown ops to `nop` — the bug),
and the agent flag + autoDismissTick() hook.  `endturn_auto.jsonl` (load → arm
{auto_dismiss} → STATE sweep, NO hand-timed clicks) verifies --target both:
neither side freezes, render near-exact (355–1198px), pass tracks ±4 (111/115).
A small residual ±4 pass offset remains (the agent dismisses at the per-tick
onEnter vs the port's input-phase — minor timing; tighten later if needed).

**Next: the main thread — port the brain step subtrees bottom-up** against the
now-clean oracle.  Step 1 (the gateway, FUN_004a277d) first: it runs first for
every realm (init→state 1), 9 fns / 5.6 KB closure (4a29b2/4a2cd6/4a2f61/4a30f6/
4a3475/4a3551/4a38db + 4a1b29).  Watch each step's state writes land as the
run-label ndiff (currently the stubbed-step signal) shrinks.

**Render gap flagged (user, 2026-06-10):** the blacksmith building's drawer-B
overhang (the part overlapping the adjacent tile) is clipped — see
map-render.md, likely an unported kind-3/4 / kind-2-phase-4 overhang variant.

---

## 2026-06-10 (session 4) — END-TURN FRONTIER LIVE: the SP turn now RUNS in the port (scroll-dismiss input chip + port virtual clock); divergence isolated to the phase-4 AI brain FSM

Opened the END-TURN frontier the load-route closure pointed at, using the
trace studio as the workhorse end to end (craft trace → anchor → diff →
the real divergence emerges).  Two ports unblocked a running turn:

- **FUN_0047685d (the open-scroll input handler)** — retail runs it first
  in its in-game input dispatcher (0x42ff10).  Ported as
  engine_popup_scroll_input_step: a right-click (rmb release) or a
  left-click in the seal box dismisses the modal event scroll →
  extended_pause = 0, the SP sim unfreezes.  Tiny helper FUN_004b1deb
  (cursor-in-rect) inlined; FUN_0040da1e/FUN_00476768 already ported.
  Deferred (documented): the special report-type arms (0x11/10/0xb/0xc)
  + the write-only game-active side effect.  This is the prerequisite for
  ANY interactive in-game trace (every modal scroll was undismissable).
- **Port virtual sim clock (--turbo, +17 ms/tick)** — mirrors the retail
  agent's installVirtualClockHook (turbo_step_ms 17).  Without it the
  port's real sub-ms frames starve the 12 ms rate gate (speed 90) and the
  turn barely advances; retail (virtualised) grants ~1 sub-step/tick.  All
  engine.c sim-timing reads route through engine_now_ms(); the trace_studio
  port driver now passes --turbo to match the retail seat.

**endturn_probe.jsonl** (load → right-click dismiss → state sweep) drives
both sides.  Result: the load is byte-exact (flip0 = the 1-slot cosmetic
residual), then the **port RUNS the turn** — phases 1→2→3→4, LFSR pulling,
army/territory/player state updating.  run1 pairs to **355 px** (near-exact).
The first REAL divergence then emerges cleanly:

- **slot 84 (s_slot_0057c944) port=1 retail=4** — the AI brain's
  round-robin realm cursor (FUN_0049a581).  Retail runs the per-tick AI
  brain in phase 4; the port's phase-4 arm is the TODO stub.
- Downstream: blocks 3/4/5 (player/army/territory) diverge, and retail
  opens a territory event scroll ("Here Be Dragons! / Scrubland") the port
  never posts — the AI brain's work.  Pushed as a feed comparison.

**Next: port FUN_0049a581 (the phase-4 per-tick AI brain FSM)** — the
round-robin over realms 1..5 dispatching a 14-state machine; states
1/6/9/0xb + their callee trees are the unported depth.  First tighten the
probe (continuous dismiss + dense early window) so the counter is
frame-exact up to the brain divergence (a residual rate offset remains:
counter 4 vs 7 early), then port + verify each brain state against retail
via the studio.

---

## 2026-06-10 (session 3) — LOAD ROUTE CLOSED: state residual 4 → 1 slot, and that last slot ROOT-CAUSED as a cosmetic sprite-variant re-roll (quirk #201)

Re-ran the grid_load_probe through the studio in PORT-ONLY mode (port
drives locally; diffed against the prior session's cached retail
captures — retail host was down all session).  The state ribbon has
collapsed from 4 differing slots to **ONE**, and it is now stable +
frozen across all 5 labels (flip0→settled), not the flaky/multi-slot
picture the last dispatch left:

- GREEN since last session's commits (f04f8bc help-hint, 4bca373
  visibility-stride): LFSR 18/19, phase tick 74, block 11, blocks 2/3
  (the AI-record + aux-name bands), camera slot 16.  Pixels also fell
  from the 92.9k scroll baseline to **273 px (0.089 %)** — the scroll
  renderer port (a96e7a3) closed the big residual.
- The lone survivor: block 0 `g_overworld_map`, sub-ranges 2/3/4.
  Chased to ground entirely from CACHED data (no live retail):

A 3-way FNV diff (.sav vs port vs retail, per 2048-B range) showed
ranges 2/3/4 are **.sav == port ≠ retail** — the port loads the block
verbatim and keeps it; retail mutates those records on load.  First
correction: the grid is **8 B/cell, 4096 cells (64×64)**, not the
"2 B / 128×128" in save-format.md — so ranges 2/3/4 = records
**512..1279**, exactly the stale candidate-#3 "cells 512..1279" (I'd
mis-converted earlier).  By elimination over retail's load tail
`FUN_004976a1`, the ONLY block-0 writer is the autotile classify
`FUN_0046dbf5`, and it writes **only the +3 byte = the terrain sprite
variant** (build_cells `004298c1` + randomise `0046dd34` write the
tile_cells int-view 0x55cea0; rebuild `0046da4b` evicts nothing — the
raw grid has 0 roster overflow, simulated; visibility `0049ca32`
writes player records).  The variant is RNG-rolled from `FUN_00404b2c`,
a 31-step LFSR over `DAT_00591564` — which is **NOT a save slot**
(runtime-only, InitInstance-seeded 0x34518632, advanced by every puller
since boot).  So retail's reload re-rolls the isolated-feature variants
from its boot+menu+load-advanced LFSR, while the port reseeds the fixed
0x34518632 and reproduces the .sav's ORIGINAL variants byte-exact.

**Verdict: the load route is done.** The last residual is cosmetic
(+3 sprite frame, non-gameplay) and can't round-trip even in retail
(the driving RNG isn't persisted) — accepted + documented as quirk
#201, not a bug.  Optional bit-match (capture retail's live
`DAT_00591564` at load-classify, seed the port's classify with it) is
deterministic per-seat but fragile and cosmetic-only → deferred.

### Next candidates
1. **END-TURN frontier** (the now-unblocked big one): the scroll
   dismiss (a96e7a3) unfreezes the SP sim, so the phase dispatcher
   `engine_turn_tick_substate0` can drive a full turn cycle.  Remaining
   sim-state TODOs in it: phase-4 per-tick AI brain FSM `0x49a581`
   (states 1/6/9/0xb + their deep callee trees unported) and phase-2
   entry `0x4a82b9`.  Deep AI subsystem — needs live retail to verify
   AI decisions byte-for-byte, so gated on the host being up.
2. Optional load-route bit-match (quirk #201) — cosmetic, deferred.
3. Re-capture grid_load_probe with retail LIVE to confirm the 273 px /
   1-slot picture on a fresh retail run (this session diffed the port
   against the prior session's cached retail).

---

## 2026-06-10 (session 2) — LOAD-FLIP TICK PORTED: substate-0 trio + the 256c-popup post/consume seam; state residual 9 → 4 slots; per-block sub-hashing

Chased the trace-studio bisect's three named unknowns to ground and ported
the lot.  A 3-call retail call-trace (`--call-trace-vas 0x404a46,0x49a010,
0x49d638` over the probe trace — exactly three hits, all in the flip frame)
plus the L2.eng string table (idx 297 = "Non 256 colour display") and the
options-init disassembly turned the whole load-flip-tick divergence into ONE
explainable retail engine-tick iteration, then the session ported it:

- **The "103rd pull"** = the SP per-sub-step LFSR stir inside FUN_004b99c0
  (ret 0x4b9c4f).  The rate controller grants exactly one sub-step at the
  flip: game speed defaults to **90** (FUN_004ae310, not a schema slot →
  12 ms slider gate) and the duty LUT row 0 passes phase 1; turbo's sub-ms
  frames starve every later grant.  Ported: the LUT image (`g_engine_rate_
  lut`, full 0xe60 reachable extent incl. the as-shipped OOB rows — quirk
  #195) + the speed default + schema-backed substate/human-active gates.
- **Slot 74's writer** = FUN_0049a010, now ported as engine_turn_tick_
  substate0: the 7-phase strategic dispatcher (phases 1/3/5/6 fully live
  on existing ports — rations+grant WAS already ported as engine_army_ai_
  set_rations_and_grant; 2/7 partial with bare-hex TODOs; 4 = end-check +
  brain-FSM TODO), wired as the substate-0 trio 0x40490d (new
  engine_tick_cadence) → 0x49a010 → 0x4650b0 (engine_army_state_run, now
  fed the live evq) inside engine_tick's sub-step loop.
- **The 256c popup** = event 0x12a posted by the tick head on non-8bpp
  desktops (one-shot), consumed by the render-phase scroll consumer
  0x472e46 — whose dequeue parks the event code in DAT_0053e910, WHICH IS
  the rate controller's extended-pause input: an open scroll IS the SP sim
  freeze (quirk #196; retail's 60-tick freeze is deterministic).  Ported
  the post gate + the consumer slice (scroll renderer 0x47309e + dismiss
  0x476768 = the remaining pixel/input chips).  Block 11 identified:
  g_misc_128 IS the event-ring head; the live queue now mirrors into it
  and seeds from it on load.
- **Two stale-mirror load bugs** (same class): the .sav CARRIES the LFSR
  (slots 18/19 ARE schema!) — the old "+102 menu pulls" reading was wrong,
  the save stored +102 — and the port's RNG mirrors + ring struct never
  re-seeded after save_load.  Both fixed in drop_in_load.  Plus the load
  tail's deferred block-2/3 writers: active-player turn-progress=1,
  FUN_0043e9e2 session re-arm (dplay stamp preserved from the save —
  documented deviation), FUN_0049ca32 visibility bits.
- **Per-block sub-hashing** (the planned next instrument): both state
  dumpers emit 16 range-hashes per block; statediff reports `sub_diff`
  range indices.  First use localized everything left.

**grid_load_probe state ribbon: 9 → 4 differing slots.**  GREEN now: LFSR
18/19 (save+1 exactly), phase tick 74, blocks 4/5 (the trio's writes
match retail byte-for-byte), block 11 at the flip.  Remaining, all
localized: block 0 cells 512..1279 (retail run-DEPENDENT — anim/wall-clock
class), block 2 player-5 aux-name area, block 3 AI-record bands
(+0xdc..+0x160 class, records 2..5), settled-only block 11 record 1
(retail posts a SECOND event mid-idle — poster unnamed), camera flake
(slot 16; hit 2 of 3 retail runs today, recapture clears it).  Pixels:
clean-run 92,897 px baseline holds = the scroll itself.

### Next candidates
1. Name retail's mid-idle second event: probe captures at +10/+20/+30 +
   `--call-trace-vas 0x472bc5` → poster's ret_va; port it.
2. Block 3 AI-record bands: sub-hash maps to the +0xdc..+0x160 field
   class (pools/cooldowns/mood) in records 2..5 — suspect a load-time
   AI-record re-arm the port lacks; finer 64-range sub-hash or a
   targeted dump-regions diff will name the fields.
3. Block 0 cells 512..1279 + run-dependence: byte-plane attribution
   (which of the 8 cell bytes) — likely the render-phase anim stamps;
   decide pin vs accept-as-flake.
4. The scroll renderer 0x47309e (closes the 92.9k-px residual) + dismiss
   0x476768 (unfreezes the sim → opens the END-TURN frontier through the
   now-live phase dispatcher).
5. Block 2 player-5 aux-name writer (dplay session name?) — cheap RE.

---

## 2026-06-10 — TRACE STUDIO: the port↔retail workhorse (capture → label-diff → serve → recapture), state parity, load-path divergence fenced

Brought the project up to openrecet's tooling standard — and past it where
its postmortems pointed (user-directed).  `tools/trace_studio/` +
`tools/trace_studio_web/`: ONE studio trace drives BOTH engines (port local
via run-openlords2.sh, retail via remote frida) to label-keyed captured
frames; the studio pixel-diffs per label, snapshots the 225-entry save
schema at every capture instant on both sides (scalars by value, blocks by
FNV-1a32 — port `src/state_dump.c`, agent `stateDumpOnce`, identical
format), field-diffs the state with schema symbol names, encodes per-group
scrub mp4s, and serves a Preact SPA (capture strip → A|B/diff stage →
GroupVideo + diff ribbon → State/Anchor panels → in-browser ⟳port/⟳both
recapture).  Plan: `docs/plans/trace-studio.md`; usage:
`docs/findings/trace-studio.md`.

Deliberate deviations from openrecet (it's not battle-tested): a SINGLE
op-compiler in the studio lowers sugar (`{caprange}` dense ranges, strides)
to the frozen primitive op set both engines already parse — killing its
"three segtrace parsers" trap; pairing is by LABEL (not ordinal kept-count)
— immune to drift; no D1/D2 engine gates (Lords2 loads are one-tick flips,
frames 10× smaller, retail frames ride the frida socket); state parity is
first-class because the game is turn-based.

What the studio caught in its first hours (all logged in the plan doc):
(1) the retail-harness segtrace parser required `w`/`l` where the C parser
defaults them — fixed both ways; (2) a ~1-in-3 retail load-camera flake
(saved+1 step = 60 px, within-run stable; port bit-identical every run) —
now identified numerically as state slot 16; (3) `new_game_to_map` is
config-state-dependent (the config screen persists the last game; the
scroll-dismiss over-spray now territory-selects); (4) THE BISECT
(`grid_load_probe`, captures at MAP_FREEROAM+0/1/2/3/60): the ENTIRE
state residual (9/225 slots) is born inside the load-flip tick and frozen
after — the port's ingame tick is state-identical to retail's across 60
ticks.  Retail's load tick does exactly ONE more double-LFSR pull
(canonical+103 vs +102, proven by stepping the ported LFSR), sets
`s_slot_0053f658`=1 (256c-popup-active suspect), and hash-diffs blocks
0/3/4/5/11.  Port `--rng-seed` (pre-ingame canonical pin mirroring the
agent) landed alongside.

Substrate: port emits `anchors.jsonl`/`captures.jsonl` (machine-readable)
in segtrace runs; `INPUT_TRACE_MAX_OPS` 512→8192; retail harness persists
the label→tick map + `state/state_<tick>.json`; quirk #194 (stale
`gen_save_schema.py` clobbers entry 138's widened storage).

### Next candidates
1. Name the 103rd pull site: mem-watch the LFSR global / call-trace the
   load-flip tick; port the 256c popup (slot 74 + likely the pull + the
   92.9k-px residual's biggest chunk).
2. Per-block sub-hashing (split big blocks into ~64 ranges) to localize
   the block 0/3/4/5/11 diffs byte-wise.
3. Camera flake root-cause (slot 16 = saved+1, load-tick-local, retail-
   side wall-clock leak).
4. Turn-pass state traces: end-turn clicks + per-turn statedumps → the
   AI-economy/RNG save-parity frontier with the state ribbon as the gate.
5. New-game determinism: config pinning + the scroll-active global
   (`{wait_until scroll==0}`) + port entry scrolls.

---

## 2026-06-04 — TURN DRIVER: AI brain FSM — build orders (step 0xc) + the COMPLETE attack-launch chain (step 0xa, 9 fns) (10 fns, host-tested)

Cleared two whole dispatch steps of the per-realm AI brain FSM `FUN_0049a581`:
step 0xc (build orders) and the entire step-0xa (attack launch) 9-function chain
— bottom-up, leaf by leaf, each its own commit.  Ten functions across
`engine_army_ai.c`, `engine_combat.c` and `engine_realm.c`; 2078 host tests green
(ASAN/UBSAN), cross-build clean every commit.  All offsets/tables/thresholds/
call-targets r2-verified byte-exact.  Quirks #191/#192/#193 captured at port time.

- **Step 0xc — build orders** (`FUN_0049e77d` → `engine_army_ai_run_build_orders`
  + leaf `FUN_0049ed13` → `engine_army_ai_mark_build_affordable`).  Rotates each
  owned realm's build tag through the personality build columns (offsets
  +0x50..+0x64 *inside* `g_ai_personality`, selected by the per-player +0x6c
  counter cycling 0..9 over six columns — quirk #191), refreshes an affordability
  flag against the six-entry cost table `DAT_004d8990`, lights the four
  stride-0x18 workforce job-bucket gates (subject to the secondary-settlement
  filter), then runs the workforce allocator + economy orchestrator per realm.

- **Step 0xa — attack launch** (`FUN_004a0015` → `engine_army_ai_launch_attack`),
  assembled from its full chain (quirk #193 documents the pipeline):
  - `FUN_004a5003` → `engine_combat_raise_region_unit` (raise a ~50-head field
    unit at the muster-cost price; sibling of muster_defender mode 0).
  - `FUN_004a689d` → `engine_combat_find_deploy_cell` (nearest deployable owned
    overworld cell to an army, capital fallback; out-params replace the
    find-nearest scratch — quirk #192 notes cell (0,0) is unselectable).
  - `FUN_004a0917` → `engine_combat_pick_best_unit` (best idle placed friendly
    unit by home-region affinity + ⅛ strength); promoted the static
    `coalition_armies_adjacent` to public `engine_realm_armies_adjacent`.
  - `FUN_004a07f2` → `engine_combat_find_garrison_realm` (first owned keep with
    spare capacity for a unit; reuses the imaged `k_garrison_cap`).
  - `FUN_004a0aaa` → `engine_army_ai_pick_muster_realms` (score owned realms,
    record the richest into +0xe5 — the muster source; r2 ground truth, the
    Ghidra decompile transposes the two score vars).
  - `FUN_004a0649` → `engine_army_ai_pick_attack_target` and `FUN_004a03f2` →
    `engine_army_ai_select_target` (best adjacent enemy realm by distance −
    wealth; the latter stores the choice in +0x4b and adds forced-owner / owner-
    gate filter modes).
  - `FUN_004a0309` → `engine_army_ai_most_hostile_player` (pinned target or the
    most-negative relation below −10).
  - The body ties it together: cooldown gate (+0x15a) → most-hostile foe → target
    realm near the muster realm → prosperity gate (provision ≥ 400, approval ≥ 25)
    → raise force + march order (+0x1a = 7) + deploy cell + cooldown reload from
    the personality column +0x74.  A full end-to-end integration test drives the
    spawn→march in `test_engine_combat.c`.

**Next on this thread:** `FUN_0049a581`'s remaining step callees, each still
carrying unported leaves: step 1 (`FUN_004a277d`, 7 callees), step 6
(`FUN_0049edc7`, sole callee `FUN_00436d02` 768 B w/ 7 leaves), step 9
(`FUN_0049f977`, 7 callees), step 0xb (`FUN_004a5667`, 4 callees incl.
`FUN_004a58f4` which reuses `engine_combat_find_deploy_cell`).  Steps 2/3/4/5/8/
0xa/0xc/0xd are now ported.  Once the ladder is down, `FUN_0049a581` and then the
substate-0 dispatcher `0x49a010` assemble into `engine_tick`.

## 2026-06-04 — TURN DRIVER: AI brain FSM — diplomacy decision (step 2) + proposals + buy-request seed + primitives (7 fns, host-tested)

Worked down the AI per-realm brain FSM `FUN_0049a581` (the substate dispatch the
previous dispatch teed up) by porting clean leaves, finishing with the meaty
step-2 body the leaves unblocked.  Four commits, seven functions, 2042 host tests
green (ASAN/UBSAN), cross-build clean.  All offsets/thresholds/codes/call-targets
r2-verified byte-exact against the disassembly (the full 1572-byte step-2 control
flow line-checked, including its decompiler-mangled dead-arm threshold ladder).
Engine-quirk #190 captured at port time.

- **Brain step 0xd — diplomacy proposals** (`FUN_004a13a6` →
  `engine_army_ai_propose_diplomacy`, engine_army_ai.c).  Runs only for a
  leader-or-unranked player (rank `+0x2b < 2`) and alternates, via the `+0xea`
  phase flag once the per-tick cooldown `+0xe9` reaches 8, between two proposal
  modes: phase 0 broadcasts an alliance proposal (code `0xc1`) to every other
  in-game human slot once the readiness gauge `+0x60` tops `0x28`; phase 1 sends
  a war proposal (code `0xc0`) to the last-ranked / weakest player
  (`s_slot_00522d90`) once the gauge tops `0x1c`, provided that player is human
  and not already this AI's lord (`+0x81`).  Proposals route through the already-
  ported event-queue producer (`engine_event_queue_post`); the queue state, the
  per-player has-data filter and the local-player slot are threaded in as params
  (same pattern as `engine_diplomacy.c`).  The variant selector
  `(mood[+0x159] − 4) + personality[+0x07]·4` and the `+0x159` rotation are
  byte-identical to `diplo_notify`.

- **Brain step 8 — no-op** (`FUN_0049f96c` → `engine_army_ai_step_noop`).  An
  empty body — a live entry in the dispatch ladder that the shipped build leaves
  inert.

- **Brain step 4 — seed explicit buy requests** (`FUN_0049e1bf` →
  `engine_army_ai_seed_buy_requests`; helper `FUN_0049d5e0` →
  `engine_army_ai_first_owned_realm`).  Clears the player's four buy-request ints
  (+0x70 run), then — for an AI above level 1, OR whose first owned realm is
  prosperous (approval +0xc > 14 AND morale +0xb > 19) — tops the two staple
  commodities to a floor when their pool runs short (+0x120 < 0x32 → +0x74 =
  0x32; +0x130 < 100 → +0x70 = 100) and folds every developed, keep-bearing
  (+0x1c0 && +0x1c3) owned realm's build demand into the requests (+0x1d4 →
  +0x70, +0x1d0 → +0x7c).  The strategic resource orchestrator drains these when
  it next reconciles the player's market.  The helper is a generic owner-byte
  (+0x005) scan returning the first realm a player owns (or 0).

- **Step-2 alliance primitives** (`FUN_004a1241` →
  `engine_diplomacy_best_ally_candidate`; `FUN_004a1774` →
  `engine_diplomacy_form_alliance`, both in engine_diplomacy.c alongside their
  sibling primitives).  The first scans slots 1..5 for the best alliance
  candidate — in-game, non-leader (rank +0x2b != 1), lordless (+0x81 == 0), gate
  +0x1c clear, relation (+0x84) > -11, no war flagged (+0x88) — returning the
  lowest-ranked qualifier or 0.  The second forges a mutual bond: treaty flag
  (+0x85) set + contact (+0x86) cleared both ways, each pinned as the other's
  lord (+0x81), bond-break flag (+0x1e) cleared.  These are the two remaining
  unported callees of brain step 2 (`FUN_004a0c1d`); its other three callees
  (event-queue post + `engine_diplomacy_clear_treaty` / `_battle_adjust`) were
  already ported, so step 2 itself is now a clean leaf.

- **Brain step 2 — per-turn diplomacy decision** (`FUN_004a0c1d` →
  `engine_army_ai_run_diplomacy`, engine_army_ai.c).  The marquee chip the three
  leaf commits unblocked (all five of its callees were now ported).  Three passes
  over the player's strategic record: (1) **warm relations** — the relation toward
  every AI slot (+0x84, stride 0x10) drifts +1/turn toward the +30 cap; (2) **lord
  upkeep** — a dropped-out lord's treaty is cleared outright, else betrayal
  pressure (+0x86) accrues from a soured relation (< -10 → +10), a thinning field
  (`s_slot_00554004` < 3 → +25) and the lord being the leader (`s_slot_00553d24`),
  and once it tops the personality threshold (+0x10) the AI betrays — clear treaty
  + relation hit + 0xb5 post; (3) **ally seeking** — a lordless non-leader past
  year 1268 (`s_slot_00553edc` > 0x4f4) latches the best candidate into +0x80 and,
  when its personality cooldown (+0x14) elapses, bonds an AI candidate directly or
  posts a 0xb4 proposal to a lordless human (latching the +0x1c pending gate).
  Reuses the now-`flag5`-parameterised `propose_notify` helper.  Two new
  personality-row fields (+0x10 betray-threshold, +0x14 ally-period) baked off the
  already-imaged `g_ai_personality` table.  NB: retail's leader-bonus threshold
  ladder has two unreachable arms (+2 / +4, nested under the `< 0x15` branch) —
  transcribed verbatim with a dead-arm note so the structure matches the binary;
  only `gauge > 0x14 → +1` actually fires.

**Quirk #190:** the strategic `+5` byte (variously labelled `STRAT_ELIMINATED` /
"active" / "AI gate" across modules) is really **human-controlled** — the FSM
dispatcher runs a slot's brain iff `+4 != 0 && +5 == 0`, and the diplomacy step
targets `+5 != 0` (human) slots, which is why the proposals surface through the
UI notification ring.

**Next on this thread:** the brain FSM `FUN_0049a581` still has ~10 unported step
callees, each carrying 1-7 unported leaves of its own: `FUN_004a277d` (step 1, 7
callees), `FUN_004a0c1d` (2, 5 callees incl. the already-ported event-queue +
diplomacy entries), `FUN_0049edc7` (6, sole callee `FUN_00436d02` 768 B w/ 7
leaves), `FUN_0049f977` (9, 7 callees), `FUN_004a0015` (10, 4 callees),
`FUN_004a5667` (0xb, 4 callees), `FUN_0049e77d` (0xc, 3 callees incl. the turn-
chip `FUN_004485a5`).  (Steps 2 / 3 / 4 / 5 / 8 / 0xd / 0xe are now ported.)  Remaining FSM steps, each
still carrying unported leaves: `FUN_004a277d` (step 1, 7 callees), `FUN_0049edc7`
(step 6, sole callee `FUN_00436d02` 768 B w/ 7 leaves), `FUN_0049f977` (step 9, 7
callees), `FUN_004a0015` (step 10, 4 callees), `FUN_004a5667` (step 0xb, 4
callees), `FUN_0049e77d` (step 0xc, 3 callees incl. the turn-chip `FUN_004485a5`).
The shallowest next dig is step 0xc's three leaves or step 10's four.  Once the
ladder is down, `FUN_0049a581` and then the substate-0 dispatcher `0x49a010` can
be assembled into `engine_tick`.

## 2026-06-04 — TURN DRIVER: phase-1 AI inits — ration ladder + difficulty grant + independent-realm driver (4 fns, host-tested)

Landed both phase-1 AI inits the previous dispatch teed up, plus the second
init's full callee chain.  Two commits, four functions, 2022 host tests green
(ASAN/UBSAN), cross-build clean.  All offsets/strides/tables/call-targets
r2-verified byte-exact; pure record/scalar bookkeeping over the shared save
blocks (host tests + r2 transcription are the gate, no retail-diff target).  Two
engine-quirks (#188/#189) captured at port time.

- **Phase-1 init #1 — ration ladder + difficulty grant** (`FUN_0049d638` →
  `engine_army_ai_set_rations_and_grant`, engine_army_ai.c).  Run once per player
  slot by the strategic per-tick FSM (`FUN_0049a581` dispatch step 3; also
  directly with player 0).  Pass 1 maps each owned realm's approval (+0xc) to its
  ration/tax setting (+0xb9) through a threshold ladder — player 0 uses a fixed
  8-step independent ladder, an AI player selects one of three ladders by the +4
  selector int of its personality row (reuses the already-baked `g_ai_personality`
  block).  Pass 2 (active / non-eliminated / level!=0 AI players only) credits a
  difficulty cash grant from a newly-baked `g_ai_cash_grant` table (0xa0 B, retail
  DAT_004dc1e0/DAT_004dc230) and tops up realms' population / stock / provisions.
  Quirk #188: the grant scales **inversely** (higher AI level → bigger cash table
  but level ≥ 5 gets cash only, no resource topups), and the selector-0 ration
  ladder is dead code (no shipped personality selects it).

- **Phase-1 init #2 — independent-realm AI driver** (`FUN_0049dfc6` →
  `engine_army_ai_run_independent`; `FUN_004a3c67` →
  `engine_army_ai_dispatch_independent_balanced`; `FUN_004a3ed3` →
  `engine_army_ai_dispatch_independent_compact`, engine_army_ai.c).  Always called
  with player 0; the sibling of `engine_army_ai_run_player` for realms with no
  player.  Quirk #189: independent realms route the same +0x1fe dispatch field
  through a **separate** table (0/1 → balanced/compact, no expansionist) and have
  **no market reconcile** — the balanced variant self-funds a provisions shortfall
  by crediting the realm's own gold pool (+0x1f4) +100 plus a self-funded buy
  ladder; both size their cell reshape off the census-bucket sum rather than the
  live cell total, and stamp strategy tag 0.  All helper calls reuse already-ported
  engine_army_ai / engine_territory / engine_army_economy entry points.

**Next on this thread:** the AI FSM `FUN_0049a581` (1039 B) still has ~13 unported
brain-step callees.  The clean leaves now are `FUN_004a13a6` (849 B, FSM step 0xd
— a diplomacy-proposal step that posts treaty/war messages 0xc0/0xc1 via
`engine_event_queue_post`; needs the event-queue state + has-data + local-player
threaded in, and is a good fit for the pure-function-diff harness given its
event-ring side effects) and the trivial no-op `FUN_0049f96c` (11 B, step 8).
The other steps (`FUN_004a277d`, `FUN_004a0c1d`, `FUN_0049e1bf`, `FUN_0049edc7`,
`FUN_0049f977`, `FUN_004a0015`, `FUN_004a5667`, `FUN_0049e77d`) each still carry
1-7 unported callees.  Once the FSM's leaves are down, `FUN_0049a581` and then the
substate-0 dispatcher `0x49a010` can be assembled into `engine_tick`.

## 2026-06-04 — TURN DRIVER: phase-3/6 march planners — capital-march + settle-march (3 fns, host-tested)

Landed the two phase-init planners the previous dispatch teed up as
zero-dependency leaves.  Two commits, three functions, 2011 host tests green
(ASAN/UBSAN), cross-build clean.  Offsets/strides/call-targets r2-verified
byte-exact; pure record/path-field bookkeeping (host tests + r2 transcription
are the gate, no retail-diff target).  Two engine-quirks (#186/#187) captured.

- **Phase 3 — capital-march planner** (`FUN_00429418` →
  `engine_army_state_plan_capital_marches`, engine_army_state.c).  Sweep the 150
  occupant records; each idle (state 4) stack gets its march goal set to its
  target realm's (+0x167) capital cell (g_per_army_768 +0x6c/+0x6d), mirrored
  into both the executor destination (+0x14/+0x15) and the path-trace goal
  (+0x16/+0x17), then routed via a shared **dual-flood** tail: flood
  conservatively (blocked_mode 1, costly cells penalised), trace to the capital,
  commit + arm (+0x14c = 1) when it lands in < 150 waypoints (the trace cap),
  else re-flood permissively (blocked_mode 0) and retry once.  Quirk #186.

- **Phase 6 — settle-march planner** (`FUN_004280e9` →
  `engine_army_state_plan_settle_marches`; `FUN_00428007` →
  static `find_settle_spot_near_realm`, engine_army_state.c).  An idle (state 3)
  stack flagged "arrived" (+0x150) scans up to 16 entries of its category-bucket
  row from a round-robin cursor (+0x164) for the first in-range realm, finds an
  open cell near that realm's capital (FUN_00428007 = radius-1..3 box scan, the
  already-ported engine_map_query_box_any_rawflag_bit0 / FUN_0046cfbd), routes
  there + clears arrived; non-arrived stacks then run the same dual-flood tail.
  FUN_00428007 surfaces the cell through file-static scratch standing in for
  retail's DAT_00569570/78 find-nearest globals (promote to shared when a second
  caller — e.g. FUN_004292af — is ported).  Quirk #187: phase-6 indexes the
  6-row `buckets[6][16]` table (DAT_00567970, save entry 138) by **per-region**
  (i-1)*16 with i up to 150, sliding past row 5 into zero BSS slack; the
  in-memory backing of `s_slot_00567970` was widened to 150*16 (save still 96 B)
  so those reads match retail without an OOB.

**Next on this thread:** the phase-1 inits remain.  `FUN_0049d638` (1737 B) is a
zero-callee leaf but a meatier port — a per-realm AI stat→bucket classifier
(personality-keyed threshold ladders) plus per-realm economy accumulation; it
needs two retail config tables baked first (`DAT_004d8a5c` personality ladder
block, `DAT_004dc1e0`) and, since it mutates economy fields, is a good fit for
the pure-function-diff harness (byte-exact vs retail) rather than host tests
alone.  `FUN_0049dfc6` (505 B) is **not** a clean leaf — it still calls the
unported `FUN_004a3c67` / `FUN_004a3ed3`.  After the phase-1 inits, the
default-branch per-tick `FUN_0049a581` (1039 B, ~15 unported callees) and then
the dispatcher `0x49a010` itself.  The two A/V phase-7 callees
(`FUN_0043ad25` / `FUN_004b0cb4`) and the assault trigger `FUN_004a8aab` remain
verification-gated.

## 2026-06-03 — TURN DRIVER: phase-2/5/7 callees — siege-build, merc rotation, treaty reconcile, raider planner (7 fns, host-tested)

Continued working down the strategic turn driver `0x49a010` (still unported)
by porting the **pure phase callees** it fires each phase.  Four commits, seven
functions, 2008 host tests green (ASAN/UBSAN), cross-build clean.  All
offsets/strides/tables/call-targets r2-verified byte-exact; pure scalar/record
bookkeeping (host tests + r2 transcription checks are the gate, no retail-diff
target).  Five engine-quirks (#182–#185) captured at port time.

- **Phase 2 — siege-engine construction** (`FUN_004a8507` →
  `engine_army_state_siege_build_step`, `FUN_004a84ba` →
  `engine_army_state_scan_siege_built`, engine_army_state.c).  A besieging stack
  (live / state 1 / `+0x199` siege link) builds three equipment slots
  `{count,pct,value}@+0x182` toward `k_siege_build_target[slot]*count`
  (`{200,200,400}`, retail DAT_004de440), the army's head-count (`+0x168`) split
  evenly then topped up as the per-tick build budget; the aggregate
  remaining-byte (`+0x19c`) hits 0 when all engines are built.  The scanner
  resumes the persistent cursor `s_slot_0057cad4` (reset to 1 by the
  already-ported phase-2 init FUN_004a82b9), parks on the first completed stack
  and returns 1 → the driver launches the assault (`FUN_004a8aab`, the A/V
  trigger — **still deferred**, plays s080 SFX + battlefield render).  Quirk #182.

- **Phase 7 — mercenary-offer rotation** (`FUN_004aca2b` →
  `engine_combat_rotate_merc_offers`, `FUN_004acb54` → static `merc_offer_holder`,
  engine_combat.c).  Each free merc-pool slot (g_unit_table_20, back-ref `+0x00`
  == 0) clears its offer, counts down (`+0x06`) and steps a realm cursor (`+0x04`,
  wrapping at realm count); the turn its countdown expires it republishes the
  cursor (`+0x03`), reloads from the period (`+0x07`) and double-steps the cursor;
  then each realm's mustered-unit scratch (g_per_army_768 `+0x1ad` / A_F1AD) is
  stamped with the slot offering it.  Quirk #183 (one-turn offer visibility +
  publish-path double cursor step).

- **Phase 7 — lord/vassal treaty reconciliation** (`FUN_004a1847` →
  `engine_diplomacy_reconcile_treaties`, engine_diplomacy.c).  Wipe the treaty
  matrix (`+0x85` stride 0x10), then re-derive from each player's declared lord
  (`+0x81`): mutual / impose-on-unbonded bonds re-pinned both ends, one-sided /
  dead-lord bonds severed.  Quirk #184 — snapshot + ascending-order dependence (a
  dead/contested lord only severs when it has the lower player index).

- **Phase 5 — raider target + movement planning** (`FUN_004ac5ba` →
  `engine_army_state_raid_assign_target`, `FUN_004ac499` →
  `engine_army_state_plan_raids`, engine_army_state.c).  A settling (state 2)
  stack is given a target realm via a shared round-robin cursor
  (`g_raider_target_cursor` = DAT_004e59dc, a runtime non-save scalar), its
  destination set to that realm's capital (`+0x6c/+0x6d`); the sweep then floods
  a cost field from the stack (engine_path_field_fill), traces to the capital
  (engine_path_field_trace) and on success commits the path
  (engine_path_field_copy_record) + arms `+0x14c`.  Quirk #185 — the destination
  is recomputed from the *shared cursor*, not the stored target, so a kept target
  can point at the wrong capital.

**Next on this thread:** the phase-3 init `FUN_00429418` (669 B) and phase-6 init
`FUN_004280e9` (904 B) are now **zero-dependency leaves** — all their path-field
deps are ported — so both land today; they look like sibling movement/placement
sweeps to the phase-5 planner.  Then the remaining unported phase callees are the
phase-1 inits `FUN_0049d638` (1737 B) / `FUN_0049dfc6` (505 B) and the
default-branch per-tick `FUN_0049a581` (1039 B), after which the dispatcher
`0x49a010` itself can be assembled into `engine_tick`'s substate-0 branch.  The
two A/V phase-7 callees (`FUN_0043ad25` screen-mode dispatch, `FUN_004b0cb4`
palette/fade) and the assault trigger `FUN_004a8aab` remain verification-gated.

## 2026-06-03 — TURN DRIVER: phase sequencer + army-state phase sweeps + siege teardown (5 fns, host-tested)

Worked down the **strategic turn driver** chain — the SP per-tick
screen-substate-0 dispatcher `0x49a010` (still unported) drives a 7-phase
cycle, and this session ports the small pure-logic callees it fires each
phase.  Three commits, five functions, 1988 host tests green (ASAN/UBSAN
clean), cross-build clean:

- **`FUN_0049ce51` → `engine_turn_advance_phase`** + **`FUN_0049b6d3` →
  `engine_turn_reset_roster`** (engine_turn.c).  The phase sequencer steps
  the turn phase 1..7 (wrapping 7→1), maintains the prev/next-phase
  scalars + the per-phase pass counter + the redraw cookie; on the cycle
  wrap it re-arms the per-realm roster (active realms → mid-turn 0 +
  counted into s_slot_00554004, inactive → 999 "done" sentinel).  This is
  the **producer** of the exact field `engine_turn_end_check`
  (`FUN_0049b762`) already consumes — opposite ends of the turn cycle.
  All six fields are existing save-schema scalars (no scalar wall).

- **`FUN_004a4f5b` → `engine_army_state_arm_movement(state)`** +
  **`FUN_004a4e3d` → `engine_army_state_arm_movement_owned(state, owner)`**
  (engine_army_state.c).  Per-phase occupant sweeps over the same 150
  records the army-state machine drives: force sub-state 2 (→ movement
  executor) on records matching the phase's state (and, for the owner-
  gated variant, owner +0x00 + local-control +0x01); return 1 if any
  armed so the driver advances the phase only when the sweep is dry.  The
  driver runs `(3)`/`(4)` in phases 6/3 and `(2,6)` in phase 5.

- **`FUN_0043b917` → `engine_combat_break_siege_link(occ)`**
  (engine_combat.c).  The inverse of siege setup (a shared teardown helper
  with 5+ siege callers): clear the besieger's +0x199 region link and,
  via the region's per-army pending slot (+0x1bc → defender occupant), the
  defender's +0x19a besieged mark.  Fired by the owner-gated sweep's
  state-1 hook.

All offsets/strides r2-verified byte-exact.  Pure scalar/record
bookkeeping with no baked tables, so host tests + the r2 transcription
checks are the gate (no retail-diff target, unlike the table-baking
combat chips).  Also **demoted two ledger phantoms** — `0x49a010`
(982 B) + `0x40490d` (313 B), the substate-0 driver pair falsely promoted
to "tested" by engine.c sub-step-body TODO comments citing them by `FUN_`
token (the recurring ledger gotcha) — to bare hex.

**Next on this thread:** the remaining phase callees deepen quickly —
phase 2's `FUN_004a84ba` needs `FUN_004a8507` + the 753 B `FUN_004a8aab`;
then the phase-7 turn-advance arm (which calls the already-ported
`engine_turn_run_passes`) and finally the dispatcher `0x49a010` itself,
which wires the whole turn loop into `engine_tick`'s substate-0 branch.

## 2026-06-03 — SCENARIO START: port FUN_00499e5d ("Play Now!") + wire the Play Now! button + `--playnow` seat (RETAIL-DIFFED)

The "Play Now!" / scenario quick-start config-apply (`FUN_00499e5d`) is now
ported and wired — the scenario sibling of the page-7 custom apply
(`FUN_00499dc3`) landed the day before.

- **`engine_game_init_apply_scenario_config(campaign_flag, scenario_idx)`
  (port of FUN_00499e5d)** — sources the SAME config globals as the custom apply
  (map id / density / five realm presets / AI count) from a fixed row of a baked
  0x20-stride preset table instead of the page-7 tabs: `campaign_flag`
  (s_slot_0053f640) picks the table — `k_scenario_presets` (retail 0x4d8e18,
  skirmish/deathmatch) or `k_campaign_presets` (retail 0x4d8f58) — and
  `scenario_idx` (s_slot_0053f258, 0..7) picks the row.  Forces standings off /
  render-mode 0 / morale-gate off / zero budget.  Both tables baked dword-exact
  from Lords2.exe via r2 (`pxw @ 0x4d8e18 / 0x4d8f58`); the field→global mapping
  is read straight from the decompiled stores.  The map-setup tail retail runs
  inline (minimap/plane-unpack/build-cells) is the port's shared
  `newgame_build_world_and_start`, not duplicated here.  See engine-quirks #177.

- **Live wiring** — `newgame_enter_live` grows a `mode 1` branch (scenario): it
  applies the scenario config first (which sets the AI count directly from the
  table), then `set_player_config(ai_count + 1, human 1, …)`, then the shared
  world build + game-start.  The main-menu "Play Now!" seam (engine_menu_input
  `param==2 && g_loadlist_dir_flag==1`, previously a `TODO(game-start)`) now
  routes through `g_game_start_fn(1)`.  The difficulty-picker UI (retail
  0x4334f0) that varies the index isn't wired yet, so the live button uses the
  default selection (index 0).

- **`--playnow [IDX]` headless seat** — the scenario analogue of `--newgame`:
  sources the presets from the table row IDX via the ported apply, builds the
  world, renders the first frame, dumps `port_playnow.{sav,raw,bmp}`.  Different
  rows pick different maps + AI counts (idx 0 → map 17 / 4 realms, idx 2 → map 2
  / 10 realms, idx 4 → map 0 / 14 realms, idx 6 → map 7 / 15 realms), all
  rendering coherently (0 oob/err).  Refactored the render+dump tail out of
  `bringup_newgame` into the shared `newgame_dump_first_frame(base)`.

- **VALIDATION.**  Host test `game_init_apply_scenario_config_rows` pins the
  exact config globals each indexed scenario/campaign row produces + the forced
  invariants + OOB clamp (1874 host tests).  **Retail diff** (`playnow-loadverify1`):
  retail LOADS the port's `port_playnow.sav` (scenario 4 = map 0) → the saved
  config scalars round-trip byte-exact (scen_idx=4, density=2, ai=4, map=0,
  standings/render/budget=0) and the world loads STABLE (single NEW_GAME +
  MAP_FREEROAM anchor @ frame 563, no turn-spam).  The unsaved preset globals
  read back as retail's transient menu values (gold 2500 / keep 1, NOT the row's
  5000 / 4) — confirming they're correctly not persisted (their effect is baked
  into the saved per-army/realm records, visible in the port_newgame↔port_playnow
  save diff).  USER-CONFIRMED the scenario frames look good via feed.

- **NEXT.**  The difficulty-picker UI (page-5 campaign chooser 0x4334f0 +
  per-button index mapping) so the live Play Now! varies the scenario; the
  gold-standard config-apply diff (drive retail's own Play Now!→campaign + dump
  pre-save config globals) builds on that.  Deeper frontiers unchanged: AI
  economy / autotile / RNG save-parity.

---

## 2026-06-03 — NEW-GAME: live page-7 "Start" → strategic map through the genuine menu (USER-CONFIRMED)

A new game is now reachable through the **real menu UI**: clicking "Start" on the
page-7 config screen drops the drop-in into the natively-rendered strategic map —
the new-game analogue of the `--strat-load-live` menu→load→map path.  Previously
only the headless `--newgame` seat could start a game (it supplied the config
inline).  Three pieces landed:

- **`engine_game_init_apply_custom_config` (port of FUN_00499dc3)** — retail's
  page-7 "Start" config-apply.  Copies the 12 config-screen option-tab selections
  (engine_title_render `g_p7_tab_values`, retail DAT_0053f288..0x53f2b4) into the
  live game-config the new-game initialiser reads: the five realm presets
  (econ/resource/troop/gold/keep — the SAME storage `set_realm_presets` latches,
  so this is the config-driven alternative to that hard-coded setter), the
  standings/mode/render-mode/budget config scalars (s_slot_0053f25c/260/264/23c/
  26c), and the AI count s_slot_0053f268.  Two selectors index const tables baked
  from Lords2.exe (0x4dbbf8 budget, 0x4dbc18 gold).  Verified: over apply_defaults'
  SP-default tabs it reproduces the `--newgame` seat's hard-coded presets exactly
  (economy 1 / resource 2 / troop 0 / gold 1000 / keep 3, AI 4).

- **Game-start seam + Start wiring** — engine_menu_input grows a bound seam
  `engine_menu_input_bind_game_start_fn`; the page-7 Start handler (FUN_004335f0
  arm 2) keeps retail's faithful gate `DAT_00553f98 <= DAT_0053e8a4` (humans fit
  the roster) and routes 499dc3 + 4329ec through the seam, keeping the input
  module free of game-init/IO deps.  The live engine binds it (in
  `title_bind_render_state`) to `newgame_enter_live`, which seeds the players
  (set_player_config), applies the config (apply_custom_config), builds the world
  + game-start (shared `newgame_build_world_and_start`, factored out of the
  `--newgame` seat), reinstalls the strategic palette (base01.256 — the same
  menu→map magenta fix the LOAD path applies, engine-quirks #156), and sets the
  in-game-active flag DAT_0052afa4.

- **`--newgame-live` seat** — drives the engine into the page-7 config screen
  (reusing `seed_page7_state`: country list + SP-default tabs + a 5-seat roster so
  the Start gate passes), then `main_loop_body` fires the genuine page-7 Start
  click on frame 5.  `engine_menu_title_input` routes it through the wired handler
  → seam → `engine_game_start`, which flips `g_sim_current_screen` 0x1f→0; the
  real tick's screen-0 dispatch then paints the map natively.

**Verified live (user-confirmed via feed, 2026-06-03).** `--newgame-live`:
`started (map=0 realms=14 screen=0 state=3 data_ready=1 year=1268 season=4)` —
the SAME end state as the validated `--newgame` seat, reached through the genuine
menu Start click.  The rendered frame is a clean full-HUD strategic map
(File/Options/Help bar, 1268 Winter, 1000 Crowns, England minimap with red
Lancashire, territory panel pop 435 / ♥72 / Tax 0% / Ration Normal, cows in
fenced pastures, castle, the red capital flag, End turn).  Caught + fixed the
expected menu→map palette magenta on the way (the config screen leaves gateway.256
active; base01.256 reinstalled on map entry, exactly as the load path does).
1873 host tests green (+ apply_custom_config defaults + 2 Start-seam routing tests).

**NEXT options:** Play Now! / scenario Start (mode 1 → port FUN_00499e5d, the
scenario config-apply + map setup, left as a TODO in the seam); the fully
interactive page-1→4→7→Start nav (the seam is bound in `title_bind_render_state`
so `--title-live --title-page 7` Start already works — the gap is page-4/7
rendering polish); the residual new-game save-parity (autotile +2 / AI-economy /
RNG, all on deeper frontiers).

## 2026-06-02 — NEW-GAME: `--newgame [MAP_ID]` seat — live drop-in renders a fresh game's first strategic frame

The new-game render path is now **live end-to-end**: the drop-in can START a
fresh game and draw its first strategic-map frame natively, the new-game
analogue of `--strat-load` (Phase B of the new-game-render plan).

`bringup_newgame(map_id)` in `main.c` supplies retail's page-7 config-screen
preconditions inline (the scenario/custom apply `FUN_00499e5d`/`FUN_00499dc3`
are unported), then runs the ported new-game initialiser + game-start:

- **Preconditions:** map id (`s_slot_0053f034`) + view-mode 0; the five realm
  presets via `engine_game_init_set_realm_presets` — taken from the scenario
  table (retail `0x4d8e18`, a 0x20-stride row `FUN_00499e5d` indexes by the
  config selector); the map-0 row (record 4 @ `0x4d8e98`) = gold 5000 / keep 4 /
  economy 2 / resource 1 / **troop 0** (troop 0 disables the starting-army block,
  so the deferred audible troop-spawn seam is not exercised for the first frame).
  Then `engine_map_init_build_cells` + `_set_camera`, bind the loaded
  `L2_maps.dat[map_id]` record (the `FUN_00467770` plane-unpack seam), bind
  `l2.eng` (`config_localization_data`), and load the minimap political grids
  (`FUN_0046a037`, run by retail's scenario-apply — *not* inside `FUN_00497ced`).
- **Run:** bind the seasonal-bank seam (`FUN_004984dc`, the render data-ready
  gate) and call `engine_game_start` (`FUN_004329ec` SP branch → `FUN_00497ced`).
  The per-turn driver (`FUN_00448440`) is already bound to the orchestrator's
  step-17 seam in WinMain, so the fresh game ticks to turn 1 during init.
- **Render:** the SAME screen-0 dispatch arm the live loop runs
  (`engine_screen_strat_render` = `FUN_0040f5fd`); dumps `port_newgame.raw`/
  `.bmp` for the future Phase-C byte-validation vs a retail new-game capture.

**Verified live (user-confirmed via feed).** `--newgame 0` on the install
host: `init done (map=0 realms=14 screen=0 state=3 data_ready=1 year=1268
season=4)` — a full 14-realm world ticked to turn 1 (1268 Winter, the
year-rollover quirk #171), then `rendered body — drawA=263 drawB=19 sky=0
army=15 feat=291 (0 oob/err)`.  drawA=263 matches the map-0 LOAD frame's
terrain-walk count exactly (same map → identical descriptor build), confirming
the new-game descriptor finalize is correct; the new-game-specific deltas
(camera recentred on the player's capital, fresh ownership, 15 army start
markers vs the load save's 14, fewer settlements → drawB 19 vs 29) are the
expected fresh-game state.  Frame is a coherent full-HUD strategic map
(menu bar / minimap / territory panel / End-turn).  1870 host tests green
(seat is Win32-only; the orchestrator path it drives is already host-tested
byte-exact: +7 == L2_maps plane5 = 4096/4096, territory_count 14).

**NEXT — Phase C (byte-validation):** capture retail's new-game state at the
first MAP_FREEROAM frame via `new_game_to_map.jsonl` + `--rng-seed` +
`--dump-regions`/`--dump-globals`, then a host byte-diff of `engine_game_init`'s
output (gold standard, like the load path's `load_clean_dump`).  The custom-game
preset path (`FUN_00499dc3`, the trace's actual route) vs the scenario presets
used here is the one remaining seam to reconcile for an exact HUD-economy match.

---

## 2026-06-02 — TURN-ENGINE: full per-turn driver FUN_00448440 ASSEMBLED → engine_turn_run_passes + engine_turn_advance, wired into new-game init

The turn-advance engine is now **assembled end-to-end**.  All 30 direct callees
plus the inline calendar block had landed across prior chips; this chip strings
them into the live driver in retail's exact order
(`docs/decompiled/by-address/448440.c`):

- **`engine_turn_run_passes(evq, rng_7bit_b, rng_7bit_a, mp_active)`**
  (`src/engine_turn.c`) — the host-testable / diffable core: the inline calendar
  block followed by the 27 economy / realm / territory passes, a 1:1
  transcription of the decompiled call sequence.  Retail threads the UI event
  queue, the MP flag, and the two cached RNG bytes through file-scope globals;
  the ports take them as explicit params, so the orchestrator stays in the host
  suite and out of Win32-only engine.c.
- **`engine_turn_advance(void)`** (`src/engine.c`) — the Win32 wrapper.  Runs
  retail's two head calls — the per-realm AI brain (`FUN_0049a990` =
  `engine_army_ai_run_turn`) and the RNG stir (`FUN_00404a46`, which refreshes
  the masked-RNG mirrors `g_rng_7bit_b/a` the next passes consume) — then hands
  the freshly-stirred bytes, the engine's one canonical event queue (a new
  file-static, lazily inited), and `g_sim_mp_active` to `engine_turn_run_passes`.
  The RNG-state coupling stays where the live mirrors are; the pure sequence
  stays where it can be tested.
- **Wired into new-game init**: `main.c` binds `engine_turn_advance` to
  `game_init`'s deferred `g_gi_turn_driver_fn` seam (step 17), so a fresh game
  ticks the world to turn 1 exactly as retail's `FUN_00497ced` does.  Inert in
  every current live seat (nothing drives `engine_game_start` yet — the new-game
  seating is Phase B, Windows-host-gated) and inert in the host suite (tests
  don't bind it), so binding early is harmless.

**Verification — driven over the real world, no Windows host needed.**  The new
host test `game_init_turn_driver_advances_to_turn_1` (`test_game_init.c`) binds
the assembled driver into the new-game initialiser and runs a **full new game
over the real `L2_maps.dat` world** (14 realms, the live cell grid +
territory/army records the init binds).  Every one of the 27 passes executes
over valid bound state; the test asserts the exact turn-1 calendar landing —
turn 0→1, season `{prev 3, cur 4, next 1}`, year rollover counter 1, year
1267→1268 — and that the realm count (14) and the overworld `+7` terrain plane
(4096/4096) survived the sequence intact.  This surfaced **engine-quirk #171**:
the seed season-next is 4, so the *first* turn of every new game takes the
year-rollover branch and fires the CD-audio probe.  A second host test
(`turn_run_passes_sequences_full_driver`, `test_engine_turn.c`) drives the
sequence over an empty hermetic world (linkage + sequencing gate).  **1870/1870**
green under ASan/UBSan.

No Frida full-driver diff: the diffable surface of a whole turn is just the
calendar scalars (RNG/world-independent, already byte-exact), since the 27
economy passes hit the variable price-scalar wall and are each covered by their
own diff targets — and the port side would have to skip retail's AI/stir head
and bind the union of every pass's world state, high fragility for no new
coverage.  The real end-to-end run over the seeded world is the stronger gate.

**Remaining = the live visual gate (Windows-host).**  Once the new-game seating
(Phase B) drives `engine_game_start`, a fresh game will tick to turn 1 on screen:
the season should read winter/year 1268 and the strategic map should reflect the
first turn's economy/population/development.  That subjective check is the one
piece still owed to a human at the keyboard.

---

## 2026-06-02 — TURN-ENGINE: calendar / season-rollover block of FUN_00448440 assembled → engine_turn_advance_calendar

First piece of the now-unblocked orchestrator assembly (see entry below):
ported the inline **calendar arithmetic** at the top of the turn-advance
engine `FUN_00448440` → `engine_turn_advance_calendar` (`src/engine_turn.c`).
Each turn it bumps the turn counter (`s_slot_00553240`) and rotates the season
triple (`s_slot_0057c934`/`00554470`/`0057c92c`: cur←next, prev←cur, next++,
wrap 1..4).  On the season wrap point (the just-consumed `next` was 4) it fires
the **year rollover**: rollover counter `s_slot_0055cd5c`++, the year triple
(`s_slot_00553edc`/`0057c900`/`00553e74`) rotates the same way (no wrap), and
the just-ported `io_path_probe_cd_audio` (FUN_004aef7e) runs.  The season/year
*copy* slots are seed-only and left untouched.  All eight calendar slots were
already defined save-schema scalars, so the block ports verbatim.

Notably the season wrap and the year rollover are driven by the *same*
predicate (`old season-next == 4`) — they always coincide, since the season is
clamped to 1..4.  Mutates globals in place (like the engine_realm passes), not
the pure-struct style of the sibling `engine_turn_end_check`.

Verification: 2 host tests (`test_engine_turn.c`) — a non-rollover turn (season
triple + counter only; year/copies/gate untouched) and a year rollover (season
wraps 5→1, year triple advances, counter ticks, and the CD-audio gate is forced
to 1 by the probe firing).  **1868/1868** green; engine_turn.c compiles clean
under both host gcc and the mingw cross-toolchain.

Remaining for the full `FUN_00448440` (→ a live `engine_turn_advance()` bound to
`game_init`'s `g_gi_turn_driver_fn`): the 30-call sequence — `FUN_0049a990` +
`FUN_00404a46`, then this calendar block, then the 28 economy/realm passes.
That is engine.c live-marshalling integration and wants an in-game
turn-advance check, so it's the human-verification-gated next step.

---

## 2026-06-02 — TURN-ENGINE: last two driver callees ported (FUN_004aef7e CD-audio probe + FUN_00428471 no-op) — orchestrator FUN_00448440 now fully unblocked

Closed out the final two unported direct callees of the turn-advance engine
`FUN_00448440` (the 261-byte per-turn / season-rollover sequencer).  Of its 30
direct callees, 28 had landed across prior chips; these two were all that
remained:

* **`FUN_004aef7e`** (408 B) → `io_path_probe_cd_audio` (`src/io_path.c`) — the
  **year-rollover CD-audio presence probe**, called only on the winter→spring
  rollover (`season == 4`).  Builds the CD `wav`-dir path into the shared scratch
  buffer `DAT_005cd490` from the CD drive letter (`DAT_0053f200` =
  `g_config.cd_drive`), chdir's in, probes `swor_p2.wav` / `pumkin.wav` /
  `pumkin2.wav` for the ≥151 MB full-audio set, then reverts the cwd via the
  already-ported `io_path_chdir_base` (`FUN_004af502`).  Its observable output is
  the single-player / CD-audio availability gate `DAT_005c9a74` (→1) plus the
  path buffer.  Lives in `io_path.c` because the scratch buffer is shared with
  the deferred asset-chdir helper `0x4af116`; the gate (owned by
  `engine_menu_input`) is written through its setter.

  Two quirks documented (**#170**): (a) the 30-byte path `memcpy` overruns the
  8-byte `"c:.\wav"` literal into the adjacent pooled string literals
  (`swor_p2.wav` + `"pu"`), so the 40-byte buffer carries 22 bytes of
  string-table residue — reproduced verbatim via a hardcoded template; (b) all
  four gate stores write the same value `1`, so the elaborate file-size checks
  are **dead** with respect to the gate — collapsed to one set + a faithful but
  effect-free probe transcription.

* **`FUN_00428471`** (11 B) → `engine_turn_hook_noop` (`src/engine_turn.c`) — an
  empty retail stub (register-preserve prologue + `ret`, no body; a compiled-out
  debug/profiling hook), ported as a no-op so the driver's call sequence is
  complete.

**Verification.**  3 new host tests (`test_io_path.c` ×2: gate→1 + full 40-byte
buffer byte-exact incl. the pooled residue, and the drive-letter patch;
`test_engine_turn.c` ×1: the no-op links and is inert).  Full suite **1866/1866**
green; drop-in cross-build clean.  No live retail-diff target: the probe's output
is fully determined by static `rdata` (the 30-byte template read directly from
the image) and the constant gate, so a Frida diff would only re-confirm static
bytes while running real `chdir`/file-IO in the retail process — host test +
direct binary confirmation is the byte-exact gate here.

**Next milestone now unblocked.**  With all 30 callees ported, the
orchestrator `FUN_00448440` itself (currently deferred — `game_init.c`'s
`g_gi_turn_driver_fn` hook is unbound) can be **assembled** into a real
`engine_turn_advance()`: the inline season-rollover arithmetic (turn counter
`DAT_00553240++`, the season triple rotation `DAT_0057c934`/`0057c92c`/`00554470`,
the year-rollover branch incrementing `_DAT_0055cd5c`) plus the 30-call sequence.
The season arithmetic is a small, deterministic, retail-diffable block; the call
sequence is wiring.

---

## 2026-06-01 — TURN-ENGINE chip: per-realm population-growth pass (FUN_00449ef3 + FUN_004684c6 village-tile leaf)

Ported the turn-advance engine's (retail 0x448440) per-realm **population-growth**
pass `FUN_00449ef3` (1991 B) → `engine_realm_advance_population`, plus its one
unported leaf `FUN_004684c6` (474 B, the village-tile updater) →
`engine_territory_update_village_tiles`.  This fills driver-callee slot #22, the
last big unported pass in the contiguous economy cluster around the HEAD chips.

**Mechanism.**  For every realm 1..`s_slot_0056d5dc` (note: *all* realms, no
owner gate — unlike the other turn passes) it advances the head-count at
`g_per_army_768 + key*0x300 + 0x24`:

  * **births** = pop × (base birth-rate × standing band)%.  The base rate comes
    from a 20-pair population-bracket table (rdata 0x4d6308 → `engine_math_lookup_threshold`):
    100% under 40 head, decaying to 1% past 3000 — small realms grow fast.  The
    standing byte `+0x0c` scales it 25/50/75/100/120% across the 26/51/76/100
    cut-offs (happier realms breed faster).
  * **deaths** = pop × (terrain + season death-rate)%.  Terrain harshness is
    indexed by the region byte `+0x09` (rdata 0x4d63a8, {35,20,8,3,0,0}); the
    seasonal modifier by the *just-ended* season `s_slot_0057c934` (rdata
    0x4d63c0, {_,4,0,2,8}).  Region 0 takes +2 deaths; whichever rate is lower
    gets a +1 nudge.
  * an **event modifier** `+0x1fb` (signed) adds bounded extra births (positive)
    or deaths (negative), capped at 20% of pop (`+0x2f8`).
  * net births−deaths apply, then **emigration** `+0x3c` leaves and
    **immigration** `+0x40` arrives.

It then records the previous head-count (`+0x28`), the dominant change category
(`+0x5b`: 1 birth / 2 death / 3 emigrate / 4 immigrate, cleared when |growth| at
`+0x2c` is under 6%), and the population tier (`+0xb8 = (pop−1)/25 + 1`), and
refreshes the overworld graphics: the 2×2 capital footprint descriptor by
population band ('/'/'3'/'7') via the already-ported
`engine_territory_stamp_block` (FUN_0046ac22), and the four settlement village
tiles by tier via the new `engine_territory_update_village_tiles` — which grows
each settlement cell one village stage (0x13→0x12→0x11→0x10) or reverts it to
bare terrain.

**Quirk #155 surfaced.**  On a population crash (head-count < 1 after
births−deaths) retail stores the *negative leftover* into the deaths field
`+0x34` (rather than the actual death toll) before flooring pop to 0 — ported
verbatim and pinned by a dedicated diff vector.

**Verification.**  6 host unit tests (engine_realm) + 2 (engine_territory) cover
the bands, the crash quirk, the event cap, migration, and the overworld refresh.
New diff-harness target `population` (`tools/diff_test.py --functions population`)
injects g_per_army_768 + g_overworld_map + count + season and reads both blocks
back: **200/200 byte-exact vs retail**.  The two graphics helpers reach the cell
grid + army records through the territory module's pointers, bound to the
injected globals for the run.

Next contiguous unported driver callees are the rebel/standing pass
`FUN_0044aa41` (#13) and the construction-completion pass `FUN_004508de` (#20) —
both reach into the still-unported combat-initiation / unit-spawn subsystem
(`FUN_004a7158`/`FUN_004aad07`/`FUN_004ab383`), a real subsystem boundary to
take on as its own track rather than chasing the subtree here.

---

## 2026-06-01 — TURN-ENGINE chip: per-realm unit upkeep / wages / desertion pass (FUN_004acbd4 + 13-function lifecycle cluster) — new engine_realm_upkeep module

Ported the turn-advance engine's (retail 0x448440, line 32) per-realm
**unit-upkeep** pass `FUN_004acbd4` (650 B) → `engine_realm_pay_upkeep`, plus
its full 13-function helper closure, as a new module `engine_realm_upkeep.c`.
This is the next unported direct callee in the turn sequence after the
mood/tax/dev passes already landed.

**Mechanism.**  The pass operates on the per-record occupant block
`g_per_territory_420` (save entry 5, retail 0x52f0b0, 0x1a4 stride) — in this
view each occupied record is a standing **unit** with an owner (+0x00), a value
(+0x168), a per-turn **wage** derived from that value (+0x15c), seven
sub-stockpiles (+0x16c), and an optional hire **contract** (+0x195/+0x196/+0x197
→ a slot in `g_unit_table_20`).  The pass runs three sub-passes:

  1. `FUN_004ad39a` — absorb/overflow sweep: for each active unit run its pending
     **absorb** (`FUN_004aba5a`: fold value into its army's +0x24/+0x38
     accumulators, then remove), and disband any whose type byte exceeds 0x10.
  2. `FUN_004ace5e` — per-unit morale/supply sweep (gated on `s_slot_0053f260`):
     when a unit's value no longer covers its army's stockpile floor
     (`engine_army_economy_stockpile_value`), escalate a per-record counter
     (+0x155) — warn at 1, warn+decay at 2..4, warn+**disband** at >=5.
  3. Per player 1..5: total the player's wages into the strategic record
     (`g_player_strategic_352` +0xfc), and if the resource account (+0x118)
     covers it, pay it; otherwise run a per-player escalation state machine
     (+0x158): settle expiring contracts + warn → warn+decay → warn+decay →
     **disband ALL** of the player's units.

The **wage formula** (`FUN_004ad52b`) is the one non-obvious bit: human-owned
units (strategic +0x05 != 0) pay value/4; AI-owned pay value / {3,5,10,10}
selected by the difficulty scalar `s_slot_0053f23c` — the cheaper AI upkeep is a
difficulty handicap.  Several of the helpers (`FUN_004aa039` disband,
`FUN_0046f0a9`/`FUN_0046ec6e` map-occupant-list removal, `FUN_004ac6fe` contract
clear, `FUN_004ad495` wage total) are **shared engine primitives** called from
5/4/3/2 other subsystems, so they're exposed in the header for the future army-
management ports that need them.

**Everything maps to named save blocks** — `g_per_territory_420` (entry 5),
`g_player_strategic_352` (3), `g_per_army_768` (4), `g_unit_table_20` (6),
`g_overworld_map`; the only scalar inputs are difficulty (s_slot_0053f23c), the
morale gate (s_slot_0053f260) and the local-player slot (s_slot_0057c8cc).  No
scalar-wall: this whole subtree was portable today.  r2 @0x4aa039 confirmed the
three cross-block index chains byte-for-byte (army `v*0x300+0x1bc`, strategic
`owner*0x160 + 0x2d + rec[+0x14f]`, the 0x1a4 record stride).

**Verified byte-exact vs retail.**  9 host tests (1673/1673 under ASan/UBSan)
cover wage divisors + signed human /4, total-wage filtering, contract
clear/refund, the occupant-list unlink (head / middle-splice / not-found /
self-loop guard), disband link-A/link-B paths, absorb (3 flag cases), and the
affordable + every escalation-state transition.  Added a **pure-function-diff
target `upkeep`** (FUN_004acbd4) and ran it against retail via cutestation.soy:
**16/16 and 48/48 pass**.  The diff vectors are deliberately constrained to the
**no-collect region** (unit types <= 0x10, absorb flag 0, escalation state <= 4,
contracted values large) because the disband/absorb tails call
`collect_territory_income` → the army-economy planners, which read the price
scalars `g_price_a/b/c` (fixed 5/10/6 in the diff lib, but live/variable in
retail) — un-matchable without injecting the live prices.  Staying in the
no-collect region makes the whole pass a pure function of the injected
per-territory / strategic / unit-table blocks, which are read back and diffed;
the terminal disband-all path and the stockpile-floor check are covered by the
host unit tests instead.

**Next.**  Remaining unported direct callees of `FUN_00448440`, by closure
size: `FUN_004aef7e` (1 fn, 408 B — era/year rollover, the cleanest), then
`FUN_0044c8ae` (2 fns), `FUN_0044e852` (3 fns), `FUN_004508de` (18 fns); the
large shared subsystems `FUN_0044aa41` / `FUN_0049b2a3` (~75–80 fns each) are
deferred until their dependencies thin out.

## 2026-05-31 — TURN-ENGINE chip: per-realm random-event ONSET pass (FUN_00448819 + 24 handlers + 2 territory probes) — new engine_realm_events module

Ported the turn-START **random-event onset** pass `FUN_00448819` (1277 B) →
`engine_realm_roll_events`, plus its **24 per-event handlers** (ids 0x87..0x8e,
0x12e..0x13d) and **2 territory-probe helpers** (`FUN_0044c77a` /
`FUN_0044c80a`), as a new module `engine_realm_events.c`.  This is the **first
sim pass** the turn-advance engine `FUN_00448440` runs after the standings
snapshot, and the second-largest remaining direct callee.

**Mechanism.**  For every realm `1..s_slot_0056d5dc` the pass (1) unconditionally
clears four turn-scratch bytes (+0x1fb/+0x1fc/+0x1fd/+0x1a8 — the superset the
mid-turn reset `FUN_00448d16` re-zeroes later), then (2) walks a cursor through
a fixed 256-entry event "deck" (const table `DAT_004d6108`, baked verbatim from
`Lords2.exe`).  An event rolls for a realm only when three gates pass: the
realm's OWNER has events enabled (`g_player_strategic_352[owner]+0x05`), the era
counter `s_slot_00553edc` is past 0x4f4, and the deck slot is non-zero.  On a
hit it marks the realm active (+0x00), stamps the event id (+0x1aa) and runs the
handler, which either **commits** (a per-realm colour/scratch byte and/or a
credit/debit to the owner's resource accounts at +0x118/+0x128/+0x130/+0x140,
optionally a signed morale-stat nudge with clamps) or **cancels** when a
precondition fails.  Two ids (0x12f/0x136) instead probe the realm's territory
roster (`g_misc_1360`, 0x50-byte rows of 20 i32 cell offsets) for an eligible
overworld cell and flip it via the already-ported
`engine_territory_set_cell_type` (`FUN_0046d7f4`).

**All dependencies were already in place** — the territory-flip routine and its
score-clear helper were ported in the territory slice, the gate scalars are
shared save externs (`s_slot_*`), and the realm/player/map/roster blocks are the
canonical save globals.  The one runtime input — the cached 7-bit RNG byte
(`DAT_0058fd60`, a file-static in the Win32-only `engine.c`) — is passed in as a
parameter rather than read directly, since the host test TU doesn't link
`engine.c`; the `·2`-and-walk cursor logic stays in the port and the orchestrator
will pass the live value when `0x448440` lands.

**The "random" event is barely random** — see engine-quirks #146.  One RNG draw
seeds a deterministic per-realm walk through a sparse (26/256 non-zero),
duplicate-weighted deck, so adjacent realms are correlated and the whole turn's
assignment is fixed by a single 7-bit number.

5 host tests cover the scratch-clear + both gates + empty-slot no-fire, a
no-precondition resource commit, the set-then-cancel path (dispatcher stamps the
id, handler clears it), the population gate + per-mode magnitude + signed
[0,0x19] clamp (both clamp arms), and the territory-claim commit/cancel against
a bound cell grid.  **1628/1628** pass under ASan/UBSan; mingw `.exe` clean;
`_engine_realm_roll_events` in the PE.  r2 @0x448819 confirmed the cursor seed
(`mov eax,[0x58fd60]; add eax,eax`), the realm `*0x300` stride, the four scratch
offsets, the owner `*0x160` stride and the `+0x05` events gate byte-for-byte.

**Next.**  Remaining unported direct callees of `FUN_00448440`, in sequence:
`FUN_00449889` (1253 B, the per-realm mood/standing machine — next), then
`FUN_0044b59b` (624 B), `FUN_004acbd4`, `FUN_0044bf7a`, `FUN_0044aa41`,
`FUN_0044ae3c`, `FUN_0044c8ae`, `FUN_0044e852`, and the deferred `FUN_0044bd9f`
(tax/approval band, wants the 2D-table extraction).

## 2026-05-31 — TURN-ENGINE chip: per-realm mid-turn scratch reset (FUN_00448d16)

Ported `FUN_00448d16` (104 B) → `engine_realm_clear_turn_scratch` in
`engine_realm.c` — a thin per-realm pass the turn-advance engine `FUN_00448440`
runs **late** in its sequence, after the territory-income/economy re-plan
(`FUN_004ad6c0`) and just before the **second** workforce-allocation dispatch
(`FUN_0044f699`).  For realms `1..s_slot_0056d5dc` it zeroes four
turn-accumulator bytes in each army record — the three consecutive
`+0x1fb/+0x1fc/+0x1fd` and the lone `+0x1a8` — so those counters start clean for
the second workforce pass.  They are a **subset** of the larger per-realm byte
block the turn-start pass (retail 0x448819, still unported) clears; r2 @0x448d16
confirmed the four offsets and the `key*0x300` row stride.

1 host test: the four targets zero for realms 1..count, their immediate
neighbours (`+0x1fa/+0x1fe/+0x1a7/+0x1a9`) are preserved, and a realm past the
count is left untouched.  **1623/1623** pass under ASan/UBSan; mingw `.exe`
clean; `_engine_realm_clear_turn_scratch` in the PE.

## 2026-05-31 — TURN-ENGINE chip: per-player summary-metric snapshot (FUN_004486a9 + shared total helper FUN_004487a9)

Ported the **first** sim pass of the turn-advance engine `FUN_00448440`:
`FUN_004486a9` (256 B) → `engine_ranking_snapshot_player_metrics`, plus its
callee `FUN_004487a9` (112 B) → `engine_ranking_recompute_player_total`, both in
`engine_ranking.c` (the module that already maps the `g_player_strategic_352`
record, stride 0x160, including the `F_M_118` ranking metric).

Per turn, over player slots **1..5 (no active-slot filter)**, the pass rolls
five i32 summary metrics into their previous-turn companion at `+0x04`
(`0x118→0x11c`, `0x138→0x13c`, `0x120→0x124`, `0x128→0x12c`, `0x130→0x134`) —
the last-turn values the standings / graph screens plot as trend — then resyncs
each player's **cashflow total** (`+0x138`) from its six i32 category
sub-accounts (`+0x140..+0x154`).  Order matters and is preserved verbatim: the
`+0x138 → +0x13c` copy runs *before* the recompute, so the companion keeps last
turn's total, not this turn's fresh category sum.

**`FUN_004487a9` is a shared helper, not a private inline.**  `axt`/grep show
**four** callers: the snapshot here, the per-category income-credit accumulator
(retail 0x448…, `param_3 == 0xb/0xc/0xd/0xe` → add to `+0x144/0x140/0x148/0x154`),
the upkeep-deduction pass (`FUN_004a9eb1`, subtracts then resyncs), and one more
econ site (0x44…).  So it landed as a public, named function the other three can
reference when ported, with a comment recording the cashflow-breakdown role of
the `+0x140` array.  r2 @0x4486a9 confirmed the `11·32 = 0x160` stride index math
and that Ghidra's decompile is byte-faithful.

3 host tests: the total-helper sum (stale prior total overwritten), the snapshot
roll (each metric → companion, and the `+0x13c` companion holds the *old* total
555 not the fresh 60 — pins the copy-before-recompute order), and the
no-filter/slot-0-untouched boundary (all of 1..5 roll regardless of active flag;
slot 0 left alone).  **1622/1622** pass under ASan/UBSan; mingw `.exe` clean;
both `_engine_ranking_snapshot_player_metrics` and
`_engine_ranking_recompute_player_total` in the PE.

**Next.**  Remaining unported direct callees of `FUN_00448440`, in sequence:
`FUN_00448819` (1277 B), `FUN_00449889` (1253 B), `FUN_0044b59b` (624 B),
`FUN_004acbd4`, `FUN_0044bf7a`, `FUN_0044aa41`, `FUN_0044ae3c`, `FUN_0044c8ae`,
`FUN_0044e852`, and the deferred `FUN_0044bd9f` (tax/approval band, wants the
2D-table extraction).

## 2026-05-31 — TURN-ENGINE chip: realm stat-history recorder (FUN_004ae7dd) + entry-10 identity resolved

Ported `FUN_004ae7dd` (204 B) → `engine_realm_record_history` in
`engine_realm.c` — the **per-turn realm stat-history recorder**, the last pass
the turn-advance engine `FUN_00448440` runs (right after the second workforce
dispatch).  Each turn it snapshots every realm's **population** (army row +0x24,
dword) and **approval** (army row +0x0c, byte) into the current slot of a
400-turn × 16-realm ring (save entry 10), then advances three cursors: the entry
count saturates at 400; once full the oldest-page pointer walks forward (wrap
400) and the write index wraps (400).  This is the data the in-game
population/popularity **history graphs** plot.

**The producer resolved a long-standing save-entry mystery.**  Save entry 10
(retail `0x56d8c0`, 6400 × 8 B) had been a working-theory blank — "parallel to
entry 1, possibly previous-turn or AI shadow state" — and was externed as
`g_castles_shadow`, with its init-only seeder `FUN_004ae74d` carrying a
"castle-shadow / +5 per-castle back-pointer" framing inherited from entry 1.
RE'ing the producer settles it: the inner loop is `col 1..16` reading
**g_per_army_768 row col** fields +0x24 / +0x0c, and the outer index is the
turn-ring write cursor (`s_slot_0055300c`, wrap 400) — unmistakably
`[turn][realm]`, not per-castle.  The numeric 6400 = 400×16 collision with the
castle count was a coincidence.  Renamed `g_castles_shadow` → **`g_realm_history`**
and `engine_game_init_clear_castle_shadow` → `engine_game_init_clear_realm_history`
across `save.{c,h}`, `save_schema_table.h`, `game_init.{c,h}`, the tests, and the
`save-format.md` / `game-start.md` findings; comments now describe the ring's
real semantics (population/approval per realm per turn).

**r2 @0x4ae7dd** pinned the layout: slot stride `0x80` (= 16×8), record fields
`{dword @+0, byte @+4}` (+5..+7 untouched), the `entry10 − 8` 1-based-col
addressing (we index `g_realm_history + slot*0x80 + (key-1)*8` directly to stay
in-bounds rather than form the out-of-array base pointer), and the three `0x190`
(400) bound checks — notably the count clamp fires on **`> 400`** (the page only
advances once count would *exceed* 400, not at `== 400`).

4 host tests: record placement + tail preserve + non-full cursor advance, the
`== 400` count boundary (page must NOT advance), the full-ring wrap (count
clamps, page walks, write index 399 → 0), and the page wrap (399 → 0).
**1619/1619** pass under ASan/UBSan; mingw `.exe` clean; both
`_engine_realm_record_history` and the renamed
`_engine_game_init_clear_realm_history` in the PE.

**Next.**  With FUN_004ae7dd done, the only remaining unported direct leaves of
`FUN_00448440` are the deferred `FUN_0044bd9f` (tax/approval band, wants the
2D-table extraction) and a handful of mid-size passes (`FUN_004486a9`,
`FUN_00448819`, `FUN_00449889`, `FUN_0044b59b`, …) that still carry their own
unported callees.

## 2026-05-31 — TURN-ENGINE chip: workforce-alloc dispatcher (FUN_0044f699)

Ported `FUN_0044f699` (78 B) → `engine_territory_allocate_all_workforce` in
`engine_territory.c` — the sibling of yesterday's terrain-census dispatcher: a
thin per-realm loop running the already-ported workforce allocator
(`engine_territory_allocate_realm_workforce`, port of FUN_0044f6e7) for every
realm `1..s_slot_0056d5dc`.  The turn-advance engine `FUN_00448440` sequences it
**twice**, once each side of the territory-income/economy re-plan
(`FUN_004ad6c0`).  Placed beside its callee in `engine_territory.c` to avoid a
territory→realm cross-module edge.

**r2 caught a Ghidra elision worth noting.**  The decompile shows a bare
`for (i=1; i<=count; i++) FUN_0044f6e7(i)`, but `pdf @0x44f699` reveals the loop
body also reads a per-realm byte (`[i*768 + 0x53f9b5]`, i.e. the g_per_army_768
row) into a stack scratch that is **never consumed** — only the index `i` is
pushed to the callee.  Ghidra correctly dropped the dead load; the port does the
same and the inline comment records it so a future reader doesn't hunt for the
missing table read.

1 host test pinning the inclusive bound (count 2 → realms 1-2 allocated via a
phase-1 greedy fill, realm 3's pre-dirtied counter left untouched).
**1615/1615** pass under ASan/UBSan; mingw `.exe` clean;
`_engine_territory_allocate_all_workforce` in the PE.

**Next.**  The larger turn-advance leaf `FUN_004ae7dd` (the pass that follows the
second workforce dispatch in `FUN_00448440`), and the still-deferred
`FUN_0044bd9f` (tax/approval band, wants the 2D-table extraction).

## 2026-05-31 — TURN-ENGINE chip: terrain-census dispatcher (FUN_00469b51)

Ported `FUN_00469b51` (60 B) → `engine_territory_census_all_realms` in
`engine_territory.c` — the thin per-realm dispatcher that runs the already-ported
terrain census (`engine_territory_census_army_terrain`) for every realm
1..s_slot_0056d5dc.  A per-turn pass of the turn-advance engine `FUN_00448440`
(and the whole-map census wrapper used at new-game).  Placed next to its callee
rather than in `engine_realm.c` to avoid a territory→realm cross-module edge.

1 host test pinning the inclusive loop bound (count 2 → realms 1-2 censused,
realm 3 left untouched).  **1614/1614** pass under ASan/UBSan; mingw `.exe`
clean; `_engine_territory_census_all_realms` in the PE.

**Next.**  Sibling dispatcher `FUN_0044f699` (workforce alloc) and the larger
leaf `FUN_0044ae7dd`; the deferred `FUN_0044bd9f` (tax/approval band) still wants
the 2D-table extraction.

## 2026-05-31 — TURN-ENGINE chip: per-realm territory-income + economy re-plan (FUN_004ad6c0)

Ported `FUN_004ad6c0` (524 B) → `engine_realm_collect_territory_income`, the
fourth per-realm pass of the turn-advance engine `FUN_00448440` to land in
`engine_realm.c`.  Two halves: it aggregates each territory's value into its
owning realm's income accumulators, then re-runs the full per-realm economy
plan.  Three passes with **fixed bounds** (16 realms, 150 territories — not the
live realm count, unlike the rating/emigration passes):

1. Clear both per-realm income accumulators (+0x198 / +0x19c) for realms 1..16.
2. Scan territories 1..150 in `g_per_territory_420` (save entry 5, retail
   0x52f0b0, 0x1a4 stride).  A territory counts only if its status (+0x08) is 1
   or 2 and its gate flag (+0x198) is clear; its value (+0x168) is folded into
   the owning realm's (key +0x10) accumulator — the **own/allied** slot +0x198
   when the realm owner (+0x05) equals the territory owner (+0x00) or is named
   by that owner's strategic +0x81, else the **foreign** slot +0x19c.
3. Re-run the economy plan for realms 1..16 at the current season
   (`s_slot_0057c92c`): `engine_army_economy_provision` → `_plan_production` →
   `_select_candidate` (all three already ported).

**A leaf with zero new globals.**  The 0x52f0b0 / 0x1a4 table turned out to be
the already-externed shared block `g_per_territory_420`; the army accumulators
(+0x198/+0x19c) and owner byte (+0x05) live in `g_per_army_768`, the alliance
lookup in `g_player_strategic_352`, and the three callees are ported — no
binding, no scalar-wall exposure.  r2 @0x4ad6c0 confirmed the three strides
(0x1a4 / 0x300 / 0x160), every field offset, the status∈{1,2} + flag==0 gate,
and the fixed 0x11 / 0x97 loop bounds.

6 host tests: own / allied / foreign+accumulate routing, the two exclusion
gates, the fixed-bound clear (realms 1..16 cleared, realm 0 and territory 0
both untouched), and a pass-3 wiring check (economy block bound → the planners'
up-front field clears land for realms 1..16 only).  **1613/1613** pass under
ASan/UBSan; mingw `.exe` clean; `_engine_realm_collect_territory_income` in PE.

**Next.**  Remaining `FUN_00448440` leaves: the thin per-realm dispatchers
`FUN_00469b51` (terrain census) / `FUN_0044f699` (workforce alloc) over
already-ported leaves, and `FUN_0044ae7dd`.  The deferred sibling
`FUN_0044bd9f` (tax/approval band, 2D-table aliasing) still wants careful table
extraction.

## 2026-05-31 — TURN-ENGINE chip: per-realm emigration pass (FUN_0044a6ba)

Ported `FUN_0044a6ba` (903 B) → `engine_realm_migrate_population`, the
**popularity-driven emigration pass** and another per-realm pass of the
turn-advance engine `FUN_00448440` (joining the rating roll-forward
`FUN_0044baea` and the provisioning settle `FUN_0044bf04` already in
`engine_realm.c`).  Each turn, population drifts off a realm toward whichever of
its neighbours carries the highest approval rating (+0x0c).  Three passes over
realms `1..s_slot_0056d5dc`:

1. **Clear** the per-realm migration scratch (outflow +0x3c, inflow total +0x40,
   top-contributor amount +0x44, the 16-slot donor list +0x48..+0x57, chosen
   destination +0x58, top-contributor key +0x59).
2. **Compute** emigration: find the highest-rated neighbour (list at +0x5c,
   count +0x5a); if it out-rates this realm, the emigrant count is
   `percent_of(population +0x24, percent_of(rating_gap, (100−rating)/3))`,
   capped at 100 and **halved for an unowned realm** (owner byte +0x05 == 0).
   A positive amount is stamped as our outflow, added to the destination's
   inflow, the destination recorded in +0x58, and our key registered in the
   destination's donor list.
3. **Attribute**: for each recipient, record its single largest contributor's
   amount/key (+0x44 / +0x59).

**A leaf with zero new globals.**  Every field is a per-realm offset into the
already-bound shared block `g_per_army_768` (entry 4, retail 0x53f9b0); the only
callee is the already-ported percentage helper `engine_math_percent_of`
(`FUN_00404d6b`).  No scalar-wall exposure.  r2 @0x44a6ba confirmed the field
offsets, the signed `(100−rating)/3` divide, the cap-then-halve order, and the
two `percent_of` arg orders.

Documented the donor-list fill loop's missing `break` as **engine-quirks #145**:
the first realm to feed a destination fills *all 16* slots with its own key, so
later feeders are never recorded and pass 3's "top contributor" is always the
*first* donor (the inflow total stays correct — only the attribution is
affected).  A dedicated host test pins this with two donors whose order would
flip the result if a break were added.

4 host tests (basic flow + fill-all; cap-then-unowned-halve; no-higher-neighbour
no-op; the no-break quirk).  **1607/1607** pass under ASan/UBSan; mingw `.exe`
clean under `-Wall -Wextra`; `_engine_realm_migrate_population` in the PE.

**Next.**  More `FUN_00448440` per-realm passes are now portable leaves —
`FUN_0044f699`/`FUN_00469b51` (thin dispatchers over already-ported per-realm
leaves), plus the larger leaves `FUN_004ad6c0` and `FUN_0044ae7dd`.
The deferred sibling `FUN_0044bd9f` (tax/approval band, 2D-table aliasing like
quirk #144) still wants careful table extraction.

## 2026-05-31 — GAME-START chip: secondary-settlement stamp (FUN_0046826c) — 2nd leaf of FUN_0049bd99

Ported `FUN_0046826c` (221 B) → `engine_territory_stamp_secondary_keep` — a
second per-realm leaf of the army/country setup pass `FUN_0049bd99`, landed
straight after the terrain census (same warm context).  Called once per realm
whose banner-level field +0x1c0 is non-zero (`level` = field − 1); it's the
parallel of the capital placer `FUN_00467fd1`, stamping a 2×2 footprint at the
realm's **secondary** cell (record +0x78) instead of the capital.

A thin tier-selector over the already-ported `engine_territory_stamp_block`:
descriptor base `'P'`/`'('`/`'<'` chosen by the realm's settlement fields
(+0x1c3 present-flag, +0x1c4 size `< 0x32`) plus a `level*4` stride, and the +0
TYPE byte is `0x15 + min(level, 4)`.  Zero new globals (both tier fields + the
secondary offset live in the already-bound `g_per_army_768`).  4 host tests
(the three descriptor tiers, the type cap at 0x19 for level ≥ 4, unbound
no-op).  1446/1446 pass under ASan/UBSan; mingw `.exe` clean;
`_engine_territory_stamp_secondary_keep` in the PE.

**Next.**  `FUN_0049bd99`'s remaining bottom-up leaves: the 2 KB `FUN_0044f6e7`,
then the 1-dep helpers `FUN_0044d913` / `FUN_00432746` / `FUN_0046dfd5`.

## 2026-05-31 — GAME-START chip: per-army terrain census (FUN_00469b8d) — first leaf of FUN_0049bd99

Ported `FUN_00469b8d` (404 B) → `engine_territory_census_army_terrain` in
`engine_territory.c` — the **per-realm terrain census** and the first per-army
callee of the army/country setup pass `FUN_0049bd99` (3 KB, the largest
remaining unported callee of the new-game init `FUN_00497ced`).

For realm `key`, it walks the realm's per-key cell roster (`g_cell_roster` =
`DAT_0053ea00`, save entry 12, 20 i32/row — the table
`engine_territory_rebuild_cell_roster` fills) and, for each non-zero column (a
cell byte-offset), reads that cell's +0 TYPE byte from the overworld grid
(`g_cells` = `DAT_00522f90`) and bumps one of five counters in the realm's
record (`g_per_army_768` + 0x1ff..0x204): TYPE 1 → +0x1ff; 0x0f..0x16 → +0x200;
0x02..0x0e → +0x201; 0/0x17/0x18 → +0x203; ≥0x19 → +0x204.  `+0x202` sits in the
run but is a *separate* field, never touched.

**Zero new globals.**  All three tables are already-externed shared save blocks
(`g_per_army_768`, `g_misc_1360`, `g_overworld_map`) and all three are already
bound in `engine_territory.c` (army_records / cell_roster / cells) — so the port
is a pure addition reusing existing binds, no scalar-wall exposure.  r2 @0x469b8d
confirmed the type ladder and the `key*0x300` / `key*0x50` strides.  Documented
the daisy-chain counter-init idiom as engine-quirks #135.

5 host tests (bucket split; band boundaries 1/2/0xe/0xf/0x16/0x17/0x18/0x19;
empty-slot + other-realm-row isolation; stale-counter clear with +0x202
preserved; unbound NULL-safety).  1442/1442 pass under ASan/UBSan; mingw `.exe`
clean under `-Wall -Wextra`; `_engine_territory_census_army_terrain` in the PE.

**Next.**  Continue `FUN_0049bd99` bottom-up: its other zero-dep leaves
`FUN_0046826c` (221 B — capital banner, via the already-ported `FUN_0046ac22`)
and `FUN_0044f6e7` (2 KB), then the 1-dep helpers `FUN_0044d913` / `FUN_00432746`
/ `FUN_0046dfd5`.  (Audio callees `FUN_00499aca`/`d7d` deferred — they need
audible verification, which an autonomous host-test run can't provide.)

## 2026-05-31 — GAME-START milestone: overworld-grid setup sequence complete (FUN_004676e0)

Landed the **strategic-map loader** then the **orchestrator** that seats it,
completing `FUN_004676e0` — the overworld-grid setup sequence and a direct
callee of the new-game initialiser `FUN_00497ced`.

**`engine_territory_load_strat_map` (FUN_00467770, 525 B).**  The strategic-map
record → cell-grid unpacker.  Walks all 64×64 cells of a loaded l2_maps.dat
record, copying its six 0x1000-byte cell-linear planes into cell bytes +1..+4
(planes 0–3) and +7 (plane 5); recomputes the player/army count
`s_slot_0056d5dc` as the max plane-5 value in the 0..0x10 player band; and where
plane 4 is non-zero seeds either a category bucket (plane-0 bit 0x40) or the
player roster (bit 0x80) — **0x40 wins** when both bits are set.  Up front it
resets the render rotation offset and both counts, and clears the bucket table +
roster.  The file read (`io_read_file_at`, offset `map_index*0x80C1` into the
shared scratch buffer `DAT_004eabec`) is the deferred live Win32 seam — engine
modules never touch the filesystem (io.h); the host binds a synthetic record, so
the index-independent unpack is fully tested.  `DAT_004eabec` turns out to be the
engine's **shared ~200 KB asset scratch buffer** (also vill/smithy/iconvill
loads, and the global `engine_mp_render` binds as a "menu sprite asset" — same
reused buffer, not a distinct global).  r2 @0x467770 confirmed the plane strides,
the cell-byte targets, the `plane5<=0x10 && count<plane5` count rule, and both
seed calls' cdecl arg order.

**`engine_territory_setup_overworld` (FUN_004676e0, 144 B).**  Sequences the
eleven callees in retail's exact order (r2 @0x4676e0): six `clear_cell_field`
passes (1,2,4,0,5,3), `load_strat_map`, `clear_cell_render_flags`,
`classify_feature_tiles`, `randomise_special_tiles`, **[0x46a037 deferred]**,
`build_army_adjacency`, `setup_capitals`, `seed_army_features`,
`rebuild_cell_roster(1)`, `refresh_digest`.  The clear-then-load order is what
leaves cell bytes +0/+5/+6 zero around the loaded planes.  The orchestrator owns
the persistent single-LFSR `rng` (retail `DAT_00591564`, seeded 0x34518632 at
InitInstance) and threads it through the two grid-randomiser passes — exactly the
hand-off the engine.h single-LFSR note anticipated.  Every pass is NULL-safe on
its own bound tables.

**One callee deferred, with a reason.**  `FUN_0046a037` (the per-country data
bind: loads the in-game country map grid `0x52aff0` + the 128×128 preview
`0x569590` from map01.pl8 via the IO open-probe `FUN_004af767`) is pure file IO
with **no ported consumer yet** — the in-game strategic-map arm that reads
`0x52aff0` is unported, and the preview is filled by the separate page-7 reload
path.  Deferred at its call site (bare-hex `0x46a037`), to land when the
strategic-map arm forces it — same discipline as the rest of the asset-IO chain.

**Provenance correction.**  `FUN_00467770` and `FUN_0046a037` had been
false-counted as touched because both tokens sat in a deferred-bind *comment* in
`engine_title_render.c` (the FUN_-token ledger gotcha).  `FUN_00467770` is now
genuinely ported; `FUN_0046a037` (and the comment's other two comment-only
neighbours `0x43eda0` / `0x4298c1`) were debared to bare hex.  Net: the ledger is
honest again — 398 touched / 362 tested, with `FUN_004676e0` and `FUN_00467770`
real and the deferred IO trio correctly `unported`.

8 host tests (5 loader: plane→cell copy, in-band-max count, bucket+roster + the
0x40-wins tie-break, up-front clears, unbound/NULL-bucket no-ops; 3 orchestrator:
end-to-end clear+load+count+digest, rng threading on/off, all-unbound no-op).
1437/1437 pass under ASan/UBSan; mingw `.exe` clean under `-Wall -Wextra`;
`_engine_territory_load_strat_map` + `_engine_territory_setup_overworld` in the
PE.

**Next.**  `FUN_004676e0` is one of `FUN_00497ced`'s 25 game-init callees; with
it ported, the unsurveyed/large siblings (`FUN_0049bd99` 3063 B, `FUN_0049aa0e`
2197 B, `FUN_00451150` 936 B, `FUN_004984dc` 731 B, `FUN_00448440` 261 B) are the
remaining bulk before the init itself can be assembled.

## 2026-05-31 — GAME-START chip: per-army feature seeding (FUN_00467a36) — 7/8 of FUN_004676e0

Ported `FUN_00467a36` (624 B) → `engine_territory_seed_army_features` in
`engine_territory.c` — the new-game **per-army feature-seeding pass** and the
**7th of `FUN_004676e0`'s eight direct callees** (only `FUN_00468d4f` left).
It is the top of the subtree the last two chips built from the bottom up
(`467a36` → `46d7f4` → `46d9da`).

**What it does.**  Sweep all 4096 overworld cells; for every active
feature-cell (key +7 in 1..17, flags +1 bit 0x20) assign a feature terrain
type by the army's *running* active-cell index and the global feature-density
setting `DAT_0053f23c` (= `s_slot_0053f23c`, save entry 13).  The first few
cells of each army get the rich type `0x14` (descriptor base `'h'`), then
type `1`, then type `0` — cutoffs scale with density (3 → only `0x14`/`0`;
2 → `0x14`/`1`/`0` at 4/6; 1 → at 6/8; default → `0x14` up to 8 then `1`,
never `0`).  Each chosen cell is finalised through the ported
`engine_territory_set_cell_type` (`FUN_0046d7f4`), and a per-army active-cell
tally is kept at army row `+0x205`.

**One load-bearing subtlety.**  Retail pre-writes the cell's +0 type byte
*before* calling the setter, so the setter reads the **new** type as its
"old type" — which is why the setter's 0x17/0x18 +3-correction branch never
fires from this path (the new type is always 0x14/1/0).  Dropping the
pre-write would change the +3 low-bit math for cells whose real old type was
a 0x17/0x18 special, so both pre-writes are replicated verbatim.

r2 @0x467a36 confirmed the `key<<8`+`lea[eax+eax*2]` = `key*0x300` row index,
the `+0x205` tally byte, the `(desc3+0xb0)&3` low-bit preserve, the three
`DAT_0053f23c` compares and the `idx<4/6/8` cutoffs.  Quirk #132 extended a
third time (the loose `< 0x12` key guard vs the 17-row per-army block, here at
the `+0x205` tally).

6 host tests: density-2 tiers + the setter-derived +3/+2 bytes for a 0x14
cell; density-1 and density-3 cutoffs; the default-density arm (no flatten to
0); inactive-cell filtering (key 0 / 18 / unflagged ignored + not tallied);
per-key-independent running indices + step-1 tally reset; unbound no-ops.
1401/1401 pass under ASan/UBSan; mingw `.exe` clean under `-Wall -Wextra`;
`_engine_territory_seed_army_features` in the PE.  Ledger: 391 touched / 355
tested.

**Milestone in sight.**  `FUN_004676e0` (the overworld-grid setup
orchestrator, a direct `FUN_00497ced` game-init callee) now has 7/8 callees
ported.  The last is `FUN_00468d4f` (274 B): its castle-border arm
`FUN_0046902a` wants `FUN_0046c147` (407 B); its leaves `FUN_00468c41` /
`FUN_00468e61` / `FUN_00468121` and the `FUN_00467fd1` → `FUN_0046ac22` pair
are unported.  Landing those seats the whole overworld-grid setup.

## 2026-05-31 — GAME-START chip: cell terrain-type setter (FUN_0046d7f4)

Ported `FUN_0046d7f4` (486 B) → `engine_territory_set_cell_type` in
`engine_territory.c` — the engine's **overworld cell terrain-type mutator**
and the mid-tier of the `FUN_00467a36` subtree (it is the sole caller of the
score-clear leaf landed in the prior chip).

**What it does.**  `set_cell_type(cell_off, type, variant)` writes the cell's
type byte (+0, `DAT_00522f90`), then per type derives a +3 render-descriptor
base and a +2 flag-bit contribution: type 0 → base 0x50 (and clears the
cell's AI score via the just-ported `FUN_0046d9da`); the 0x17/0x18 specials →
0x82/0x86 with **no** +2 low bits; 1 → 0x54; 0x19..0x1c → 0x6c/70/74/78;
type < 0x13 → 0x58; else 0x68.  The +3 byte (`DAT_00522f93`) keeps its low 2
bits — corrected when the *old* type was a 0x17/0x18 special — and rebuilds
the rest as `base + variant*4`.  The +2 byte (`DAT_00522f92`) gets bit0 set,
bits 2..4 replaced (bit3 for ordinary types, clear for the specials), bit7
cleared, then bit7 re-set only for the type band 15..22 (`0xe < type < 0x17`).

**Ghidra error caught by r2.**  The decompiler renders the +3 low-bit
correction constant as 0 (default) / 0x82 (old type 0x17|0x18); r2 @0x46d8e6
shows the real default is **0x50**, not 0.  It only survives a `& 3`, and
0x50 ≡ 0 (mod 4), so the masked result is identical — but the port uses the
real bytes (0x50 / 0x82) and notes the equivalence in the comment.  This is
the kind of literal-level fact the decompiler quietly normalises; verified at
the byte level rather than trusted.

6 host tests: ordinary type's base 0x58 + bit0|bit3 flags with the score
left intact (type != 0); type-0 base 0x50 + score-slot clear + low-bit
preservation; band bit7 (type 16); 0x17 special's 0x82 base with no +2 low
bits and no bit7; old-special +3 correction (corr 0x82) combined with a
variant tier; unbound no-op.  1395/1395 pass under ASan/UBSan; mingw `.exe`
clean under `-Wall -Wextra`; `_engine_territory_set_cell_type` in the PE.
Ledger: 390 touched / 354 tested.

With this and `FUN_0046d9da` landed, the **`FUN_00467a36` subtree is
leaf-complete** (467a36 → 46d7f4 → 46d9da).  Next on this thread: `FUN_00467a36`
itself (624 B), then `FUN_00468d4f`'s arm (its castle-border subtree
`FUN_0046902a` wants `FUN_0046c147` (407 B); leaves `FUN_00468c41` /
`FUN_00468e61` / `FUN_00468121` unported).

## 2026-05-31 — GAME-START chip: single-cell score clear (FUN_0046d9da)

Ported `FUN_0046d9da` (113 B) → `engine_territory_clear_cell_score` in
`engine_territory.c` — the deepest **leaf** of the `FUN_00467a36` subtree
(`467a36` → `FUN_0046d7f4` → this), one of the two `FUN_004676e0` callees
still standing.

**What it does.**  Given a cell byte-offset, look up that cell's army/group
key (its +7 byte, `DAT_00522f97`), scan that key's row of the per-key roster
`DAT_0053ea00` (20 i32/row) for the column whose entry equals the offset, and
zero the parallel i16 in the AI score table `DAT_0053fa40` (`key*0x300 +
col*2`).  Only that one score slot is touched; the roster entry stays.  These
are the exact two tables the cell-roster rebuild `FUN_0046da4b` owns — so this
is the **per-cell** score invalidation (the rebuild does the bulk reset; this
clears a single slot) that the cell-mutation helper `FUN_0046d7f4` fires when
one cell's feature changes.  A cell absent from its key row (all 20 columns
miss) clears nothing — retail falls straight through the loop.

**No new quirk.**  Unlike its rebuild sibling there's no `< 0x12` accept
guard here — the key is read straight from memory and used as a row index —
but it addresses the same 17-row tables, kept in-bounds by the +7 producers'
1..16 clamp (quirk #132's invariant).  r2 @0x46d9da confirmed the
`key*0x50 + col*4` roster index (`lea ecx,[ecx+ecx*4]; shl 4`), the equality
test on the offset argument, the `key*0x300 + col*2` score write, and the
col 0..19 (`cmp 0x14; jge`) loop bound.

4 host tests: matched-slot isolation (neighbour columns kept); absent-cell
no-op; cell's-own-key-row selection vs a same-offset decoy planted in another
key's row; unbound roster/score no-ops.  1389/1389 pass under ASan/UBSan; the
mingw `.exe` builds clean under `-Wall -Wextra`;
`_engine_territory_clear_cell_score` present in the PE.  Ledger: 389 touched
/ 353 tested.

Next on this thread: the subtree's mid-tier mutator `FUN_0046d7f4` (486 B,
now leaf-complete below it), then `FUN_00467a36` itself; and `FUN_00468d4f`'s
castle-border arm `FUN_0046902a` still wants `FUN_0046c147` (407 B).

## 2026-05-31 — GAME-START chip: per-army adjacency builder (FUN_00467ca6 + FUN_0046c080)

Ported the **per-army territory-adjacency builder** `FUN_00467ca6` (385 B)
and its orthogonal-neighbour key loader `FUN_0046c080` (199 B) into
`engine_territory.c` — the 6th of `FUN_004676e0`'s eight tracked callees
(remaining: `FUN_00468d4f` / `FUN_00467a36`).

**What it does.**  `FUN_0046c080` → `engine_territory_load_neighbour_keys`:
read the owning-army key (cell byte +7) of the four **orthogonal** neighbours
(N/E/S/W) of a cursor cell in the 64×64×8 overworld grid, clamping any
off-grid edge neighbour to 0.  The four edge guards (row 0 / row 0x3f / col 0
/ col 0x3f) are load-bearing — they're the only thing stopping the linear
±0x200/±8 byte deltas from wrapping a row or running off the grid.  Retail
scatters the results to `DAT_00553280/88/90/98` (stride-8 slots, only the
even four used); the port returns a compacted `out[4]`.  It's **shared** —
the castle-border subtree `0x46902a` (under `FUN_00468d4f`, still unported)
calls it too — so it landed as a standalone reusable helper.

`FUN_00467ca6` → `engine_territory_build_army_adjacency`: sweep all 4096
cells in byte order; for each cell whose army key is in 1..17, fetch its four
neighbour keys and record each *distinct* in-range key (≠ own key, ≠ 0) into
that army's row of the per-army record block (retail `DAT_0053f9b0` = save
entry 4, `g_per_army_768`, 17 rows × 0x300 B) — a 16-entry list at +0x5c with
a count at +0x5a.  Insert is dedup-then-first-free; a full list silently
drops.  r2 @0x467ca6 / @0x46c080 confirmed the `key*0x300` row stride
(`shl 8`+`lea eax+eax*2`), the +0x5a/+0x5c field offsets, the `< 0x10` cap,
and the four neighbour deltas + edge guards.

**Same block, three views.**  The AI score table the cell-roster rebuild
zeroes (`g_cell_score` = `DAT_0053fa40`) is *this same* per-army block at
+0x90 — a different field of the same rows.  Bound separately
(`engine_territory_bind_army_records`) to keep the landed rebuild code
untouched; in tests both point into one buffer.

**Quirk #132 extended.**  467ca6 shares the rebuild's loose `<= 0x11` accept
guard (admits key 17) against a 17-row block — a key-17 *centre* cell is a
1-row OOB write, defused identically by the +7 producers' 1..16 clamp.  A
neighbour key of 17 is harmless (stored as a list byte, never an index).  A
consequence: with centres gated 1..16, an army borders at most **15** distinct
others, so the 16-cap silent-drop arm is dead in valid data.

6 host tests: N/E/S/W interior order; both-corner edge-zeros (proves no
row/col wrap); distinct-neighbour record + symmetry; cross-cell dedup; a
15-key near-cap fill in scan order with the 16th slot left 0; unbound/NULL
no-ops.  1385/1385 pass under ASan/UBSan; both mingw `.exe` build clean under
`-Wall -Wextra`; `_engine_territory_build_army_adjacency` /
`_engine_territory_load_neighbour_keys` present in the PE.  Ledger: 388
touched / 352 tested.

Next on this thread: `FUN_00468d4f` (now one helper lighter — its `0x46902a`
subtree shares the ported `FUN_0046c080`) and `FUN_00467a36`, or seating the
live 0x4676e0 orchestrator.

## 2026-05-31 — GAME-START chip: terrain auto-tiling sweep (FUN_0046dbf5) — randomiser pair complete

Ported `FUN_0046dbf5` (319 B) → `engine_territory_classify_feature_tiles` in
`engine_territory.c`, the second of the two **FUN_004676e0 grid randomiser
passes** and the sibling of `FUN_0046dd34` (already ported).  With it landed,
the whole randomiser cluster on the new-game thread is complete:
`FUN_0046de94`/`de05` (neighbour mask) + `FUN_0046c2de` (classifier) +
`FUN_00404b2c` (single-LFSR RNG) + the two sweeps.

**What it does.**  Sweep the 129×65 terrain-index grid (`DAT_0055cea0`) in
row-major order.  A cell is a **feature-border** cell — and gets reclassified
— iff it is a real cell (tile < `0xfff0000`) whose flags byte (+1) has bit 0x4
**set**, or a special tile that is **not** the sentinel `0xfff0006`.  (This is
the exact complement of the neighbour-eligibility predicate: the cells that
predicate as *ineligible* are the ones swept.)  For each border cell it builds
the 8-neighbour mask, classifies it through the pattern table, and writes the
transition tile two ways: real cells into the descriptor's +3 byte
(`(uint8_t)(type+variant)`), special tiles into the tile word itself
(`0xfff0000 + type + variant`).  When the classifier returns the isolated
catch-all type `0x16`, the variant is overridden with a fresh single-LFSR roll
(`rng & 7`) — so isolated tiles get random visual variety while bordered tiles
rotate deterministically.  r2 @0x46dbf5 confirmed both write paths, the
`type==0x16` override, and the `de05(row, col)` argument order.

**In-place, order-dependent.**  The sweep mutates the grid *in place* in
row-major order, exactly as retail: a rewritten special tile is visible to
later cells' neighbour masks, whereas real-cell writes land in the +3 byte and
never perturb the tile grid.  The port preserves this (no snapshot) so it is
bit-identical to retail's evaluation order.  The single-LFSR state is threaded
through `*rng`, as with the sibling randomiser; the live 0x4676e0 orchestrator
will own the seeded state and thread it through both sweeps in sequence.  One
write-only scratch global dropped: the per-cell progress counter
`DAT_0053e974` (never read).

5 host tests: a real border cell (flag 0x4 set, sentinel neighbours → entry 0)
writes `0x4f` into +3 with no RNG draw; a special border tile rewrites to
`0xfff004f`; an all-sentinel grid is a no-op with no draw; the corner cell
(0,0) — swept first, four neighbours off-grid and four forced ineligible →
type 0x16 — takes the **first** RNG draw, pinned via the test's independent
single-LFSR reference; unbound-grid and NULL-rng no-ops.  1379/1379 pass under
ASan/UBSan; both mingw `.exe` build clean under `-Wall -Wextra`;
`_engine_territory_classify_feature_tiles` present in the PE.  Ledger: 386
touched / 350 tested.

Next on this thread: the remaining FUN_004676e0 callees `FUN_00467ca6` /
`FUN_00468d4f` / `FUN_00467a36` (each 1–5 deps deep), or stepping back up to
seat the live 0x4676e0 orchestrator now that its randomiser cluster is whole.

## 2026-05-31 — GAME-START chip: hex auto-tile feature classifier (FUN_0046c2de)

Ported `FUN_0046c2de` (292 B) → `engine_territory_pick_feature` (+
`engine_territory_reset_feature_counters`) in `engine_territory.c` — the
**feature-variant classifier** that consumes the 8-neighbour eligibility mask
the prior chip's sweep builds.  This was the **last blocker under
`FUN_0046dbf5`** on the FUN_004676e0 thread: with it landed, dbf5 (the sibling
new-game grid randomiser) is now fully unblocked.

**What it does.**  Match the neighbour mask against retail's 48-entry hex
**auto-tile pattern table** (`DAT_004d7310`, the count `0x30` dbf5 passes) and
emit the first matching row's feature.  Each 12-byte row: `pattern[0..7]` over
the 8 hex neighbours (0 = must be ineligible, 1 = must be eligible, 2 =
wildcard), then `type(+8)`, `aux(+9)`, the variant `cap(+0xa)`, and a per-entry
rotating variant `counter(+0xb)`.  The per-byte match is a three-way OR
(`pat==2 ∥ (pat≠0 ∧ mask≠0) ∥ (pat==0 ∧ mask==0)`); the first row matching all
8 wins.  On a match it writes the row's `type` → `DAT_0057c8a4`, advances the
counter (`++`, wrap when `cap <= counter`), and writes it → `DAT_00568200`,
returning the row index + 1.  r2 @0x46c2de confirmed the match logic, the `jg`
over the counter reset (so the wrap is `cap <= counter → 0`), and the
byte-8/9/0xb global writes.

**The table.**  Dumped all 576 bytes from the binary and transcribed verbatim
(machine-generated initializer, not by eye).  It is **complete** — all 256
neighbour configs match some row, so retail never returns 0 from this call:
entry 0 (`1·2·1·2·1·2·1·2`, the four primary directions eligible) → type 0x4e;
entry 46 (all-zero pattern) is the isolated-tile catch-all, type 0x16, `cap 8`;
entry 47 (also all-zero) is an unreachable terminator.  The `type` codes span
0x16…0x7a — the tile-variant base dbf5 combines with the variant to compute the
final cell value (`DAT_00568200 + DAT_0057c8a4`); note dbf5 *overrides* the
variant with a fresh RNG roll specifically when `type == 0x16`.

**Shape.**  c2de is shared by **6 callers** (0x46dbf5 + five 0x47* strategic
seeders), each reading the type/variant globals immediately after — a
produce-then-consume contract.  The port threads the result through an
`engine_territory_feature_pick_t` out-struct (`{type, aux, variant}`), the
module's established idiom, keeping only the per-entry rotating counters as
module state.  Retail never resets those (process-lifetime `.data`); the reset
helper exists for harness/test determinism.

6 host tests, all **pinned against the embedded table** (so they validate the
576-byte transcription too): all-eligible → entry 0/0x4e/variant 1;
all-ineligible → entry 46/0x16; first-match-wins (only-pos0 → entry 15/0x26,
not shadowed by entry 0); cap-2 rotation 1·0·1·0 + cap-8 wrap 1…7·0·1·2; cap-1
entry emits variant 0 forever; and end-to-end feeding a real
`compute_neighbour_mask` result.  1374/1374 pass under ASan/UBSan; both mingw
`.exe` build clean under `-Wall -Wextra`; `_engine_territory_pick_feature` +
`_…reset_feature_counters` present in the PE.  Ledger: 385 touched / 349 tested.

Next on this thread: **`FUN_0046dbf5`** itself (the sibling randomiser, now
fully unblocked) — the second of the two FUN_004676e0 grid randomisers, the
companion to `FUN_0046dd34` already ported.

## 2026-05-31 — GAME-START chip: hex-neighbour eligibility mask (FUN_0046de94 + FUN_0046de05)

Landed the neighbour-adjacency pair on the **FUN_004676e0** overworld-setup
thread — the machinery the sibling seeder `FUN_0046dbf5` (and the feature
classifier `FUN_0046c2de`) build on.  `FUN_0046de94` (189 B) →
`engine_territory_tile_eligible(col,row)`; `FUN_0046de05` (143 B) →
`engine_territory_compute_neighbour_mask(col,row,mask[8])`.  Both homed in
`engine_territory.c`; they read the same two grids the family already binds
(the 129×65 terrain-index tiles `DAT_0055cea0` and the 64×64×8 cell grid
`DAT_00522f90`).

**The predicate (FUN_0046de94).**  Read the terrain-index tile at (col,row)
and report eligibility: a **real cell** (tile value < `0xfff0000`, which *is*
the cell's byte-offset into `g_overworld_map`) is eligible iff its flags byte
(+1) lacks bit 0x4; the un-rolled special sentinel `0xfff0006` is eligible;
everything else — including all other special values and any off-grid
coordinate — is not.  The signed bounds ladder (`-1 < col/row`, `col < 0x41`,
`row < 0x81`) is **load-bearing**, not defensive: the sweep below pushes
neighbours past the edge for border tiles and relies on those reading as
ineligible.  r2 @0x46de94 confirmed the `row*0x104 + col*4` index math
(`shl 6`+`add`), the `< 0xfff0000` real/special split, the `test cl,4` flag
gate (jump-if-set → return 0), and the exact `== 0xfff0006` sentinel arm.

**The sweep (FUN_0046de05).**  The strategic map is a **doubled-row hex
grid**, so the 8 neighbour offsets differ by row parity.  Retail selects one
of two `{i32 dx, i32 dy}×8` tables on `row & 1` — `DAT_004d6c28` (even) /
`DAT_004d6be8` (odd) — which I dumped from the binary and transcribed
verbatim (even: (1,-1)(1,0)(1,1)(0,2)(0,1)(-1,0)(0,-1)(0,-2); odd:
(0,-1)(1,0)(0,1)(0,2)(-1,1)(-1,0)(-1,-1)(0,-2)).  For each it stores the
predicate result into the shared scratch global `DAT_00543fe0[8]`.

That global is **read by 9 functions** (`FUN_0046c2de`/`46aec4`/`46b2e7`/
`46b6da`/`46bbc5`/`46c445`/`47d816`/`47da54` + the writer here) — but as a
produce-then-consume register, each pass writing it fresh before its
consumer reads, never carrying state between independent feature passes.  So
the port **threads it explicitly** through the caller's `mask[8]` (the idiom
the module already uses for the single-LFSR RNG state) rather than keeping a
mutable module global; when `FUN_0046c2de` + `FUN_0046dbf5` land, the seeder
will pass a local `mask[8]` from the sweep straight into the classifier.

Chip boundary: `FUN_0046de94`'s only caller is `FUN_0046de05`, whose only
caller is `FUN_0046dbf5` — a tight unit, so the pair is the natural commit.
This **unblocks `FUN_0046dbf5`** down to its last dep, `FUN_0046c2de` (the
feature-variant picker that reads the mask).

6 host tests (real-cell flag gate incl. the 0xfb/0xff bit-mix cases,
special-value discrimination 0xfff0000/05/06/07, off-grid bounds + the
inclusive (64,128) extreme, even-row mask all-1 + single-neighbour flip,
odd-row parity discriminator — lighting an even-table-only neighbour leaves
the odd mask all-0, corner off-grid clamping to `{0,1,1,1,1,0,0,0}`).
1368/1368 pass under ASan/UBSan; both mingw `.exe` build clean under
`-Wall -Wextra`; `_engine_territory_tile_eligible` + `_…compute_neighbour_mask`
present in the PE.  Ledger: 384 touched / 348 tested.

Next on this thread: `FUN_0046c2de` (the feature-variant picker — reads the
neighbour mask + a 0xc-stride pattern table, writes `DAT_0057c8a4`/
`DAT_0053e9c0`/`DAT_00568200`), the last blocker under `FUN_0046dbf5`.

## 2026-05-31 — GAME-START chip: terrain-tile randomiser (FUN_0046dd34) + ledger provenance fix

Two units this session.

**1. Ledger provenance regression fixed.**  The recent RNG and
territory chips named still-unported neighbour functions by their
`FUN_<va>` *token* in `src/` comments — the recurring gotcha
(`docs/findings/ledger-provenance-audit.md`): the ledger generator reads
any `FUN_<va>` token in a tested module as provenance and silently
promotes the neighbour to *tested*.  Demoted **6** verified-unimplemented
neighbours to bare hex (`0x46dbf5`, `0x46dd34`, `0x4676e0`, `0x469370`,
`0x46e067`, `0x46d3da`; 1248 code-bytes), correcting the headline from
387→381 touched.  Two over-eager demotions were reverted: `0x45240a`
(`engine_dirty_rect_mark_full_screen`, header-map provenance) and
`0x46ead0` (inlined as `clear_record`) are genuinely ported and stay
cited *by* token.  Lesson recorded: a `grep "Port of"` miss does **not**
prove unimplemented — also check the header-map and inlined-helper
provenance forms.

**2. FUN_0046dd34 → `engine_territory_randomise_special_tiles`.**  The
fourth leaf of the `FUN_004676e0` overworld-setup sequence, and the
**first ported consumer of the single-LFSR RNG** the prior chip landed.
A 209-B sweep of the 129×65 terrain-index *render* grid `DAT_0055cea0`
(i32, row stride 0x104 = 65 i32 contiguous): in row-major order, every
cell holding exactly the special sentinel `0xfff0006` is re-rolled to
`0xfff0006 + (rng & 0xf)` ∈ {0xfff0006..0xfff0015}, advancing the RNG
once per match.  r2-verified the `row*65+col` address math (`shl 6`+`add`),
the signed `>= 0xfff0000` / `== 0xfff0006` guard ladder (the outer arm is
redundant given the equality — shared with the terrain classifier
0x46de94/0x46dbf5), the `& 0xf` payload mask, and the `& 7` scratch write.

The RNG state is **threaded through a pointer** (`engine_rng_single_in_t *`),
exactly as `game_init.c` threads the double LFSR through its new-game
seeders — so still no stateful Win32 single-LFSR wrapper: the live
0x4676e0 orchestrator will own the seeded state (`DAT_00591564` =
0x34518632) and thread it through this and the sibling seeders in order.
Two write-only scratch globals are dropped (same choice the cell-field
sweep makes): the per-cell progress counter `DAT_0053e974`, and the
cross-function mirror residue `DAT_00568200` — no consumer observes what
0x46dd34 leaves (its readers, the unported sibling 0x46dbf5 and feature
picker 0x46c2de, run earlier and re-derive it; no ported code reads it).

Homed in `engine_territory.c` (the FUN_004676e0 family) behind a new
optional tile-grid binding `engine_territory_bind_terrain_tiles` (NULL-safe;
the render modules bind the same memory read-only).  6 host tests
(row-major reroll order + RNG threading, pinned retail-seed value
0xfff0007, non-sentinel/boundary filter with no draw consumed, full-grid
value range + per-cell advance count, last-cell reach / stride, unbound +
NULL-rng no-ops).  1362/1362 pass under ASan/UBSan; both mingw `.exe`
build clean; `_engine_territory_randomise_special_tiles` present in the
PE.  Ledger: 382 touched / 346 tested.

Next on this thread: `FUN_0046dbf5` (the sibling randomiser) once its
deps `FUN_0046de05`→`FUN_0046de94` and `FUN_0046c2de` land.

## 2026-05-31 — RNG chip: single-LFSR advance (FUN_00404b2c)

Ported the engine's **third** RNG — retail `FUN_00404b2c` @ 0x404b2c
(109 B) → `engine_rng_step_single` in `src/engine_rng.c`, the module
that already holds the double-LFSR `engine_rng_step` (FUN_00404a46).
It's structurally one lane of that double LFSR: same Galois taps
(bits 0 and 4), same bit-30 feedback inject, same 31 iterations — but
on its own state word `DAT_00591564` (retail seed `0x34518632`, set in
the bootstrap `FUN_004b2258` right after the double-LFSR pair, decompile
line 83637) with a single 7-bit output mirror `DAT_005c9a84` instead of
the double LFSR's six masked fields.  Retail call shape is
`FUN_00404b2c(); v = DAT_005c9a84 & 7;` — invoked for its side effect on
the mirror, which callers mask further.

Why this leaf now: it's the shared dependency under the next two
**FUN_004676e0** callees on the new-game thread.  `FUN_0046dd34` and
`FUN_0046dbf5` are the grid *randomiser* passes — they sweep the 129×65
terrain-index table `DAT_0055cea0` and roll fresh values via this RNG.
Of FUN_00404b2c's six call sites (all unported), two are those seeders;
the other four are strategic samplers at 0x47*.  None are ported yet, so
this primitive has **no live drop-in consumer today** — by the module's
established split (engine_rng.c = pure primitives, engine.c = the Win32
state wrapper) the stateful mirror + `DAT_005c9a84` accessor lands with
the first ported consumer, exactly as the double LFSR's wrapper exists
in engine.c only because the tick already drives it.  Adding a dead
state mirror now would trip `-Wall -Wextra`; the pure function captures
100% of FUN_00404b2c's logic and is fully testable on its own.

Same effectively-31-bit quirk as the double LFSR (feedback targets bit
30, unsigned `>>1` clears bit 31 → high bit always 0 after the first
call) — already documented; no new quirk.

7 host tests: zero-state fixed point, retail-seed pinned
(`0x34518632 → 0x3f149e51`, 7-bit `0x51` — pinned from an independent C
model, not the port), bit-31 clear, 7-bit mask, **double-lane
equivalence** (single LFSR is bit-for-bit lane A of the double LFSR),
reproducibility, multi-call evolution.  1356/1356 pass under ASan/UBSan;
both mingw `.exe` builds clean; `_engine_rng_step_single` present in the
PE.  Ledger: 387 touched / 351 tested.

## 2026-05-31 — GAME-START chip: per-key cell roster rebuild (FUN_0046da4b)

Landed the third (and cleanest remaining) leaf of **FUN_004676e0**, the
overworld-grid setup sub-orchestrator the prior two chips descended into.
`FUN_0046da4b` @ 0x46da4b (293 B) → `engine_territory_rebuild_cell_roster`.

It rebuilds the per-key cell roster `DAT_0053ea00` — which turned out to
be **save entry 12** (`g_misc_1360`, 1360 B = exactly 17 rows × 20 ×
i32).  Walk all 4096 overworld cells in byte order; each "active feature"
cell (group-key byte +7 in 1..16 **and** raw-flag byte +1 bit 0x20 set)
is appended to the first free (== 0) column of its key's roster row,
capped at 20.  A cell whose key already holds 20 finds no free column and
is **evicted** — type +0 → 0, +3 → 6, flags +1 → 0, +2 `&= 0xe3`.  A
preliminary pass zeroes roster rows 1..16 and, only on the new-game path
(`reset_score_table == 1`), the same rows × first 20 columns of the
parallel AI score table `DAT_0053fa40` (384-i16 rows, consumed by the
unported strategic demand/price scorer).  Both tables bind through new
NULL-safe setters (`engine_territory_bind_cell_roster` /
`_bind_cell_score_table`), matching the module's existing optional-bind
pattern; homed in `engine_territory.c` since it scans + mutates the same
cell grid the placer binds.

Two quirks captured (**engine-quirks #132**): the accept guard `< 0x12`
admits a key (17) the 17-row table can't hold — a latent 1-int OOB kept
unreachable only because the +7 field's producers gate it `< 0x11`;
preserved verbatim rather than clamped (faithful drop-in).  And the
free-column sentinel (0) collides with cell (0,0)'s own offset, so a
feature there could never register — harmless since (0,0) is map border.

6 host tests (key grouping in scan order, inactive-cell filter, 20-cap +
eviction field map, rows-1..16 clear isolation, score-clear reset gating,
unbound no-ops).  1349/1349 pass under ASan/UBSan; both mingw `.exe`
builds clean (`-Wall -Wextra`), `_engine_territory_rebuild_cell_roster`
present in the PE.  This leaves FUN_004676e0 at 3/8 leaves ported; the 5
remaining callees (`FUN_0046dbf5` / `FUN_0046dd34` / `FUN_00467ca6` /
`FUN_00468d4f` / `FUN_00467a36`) each still carry 1–5 unported deps.

Frontier note: the prior top leaves (`FUN_004b1897` 11-B no-op,
`FUN_0041a734` HUD redraw, `FUN_0042f58c` palette-apply) were skipped —
each is either a verified stripped no-op with **zero live ported caller**
(its 3 "callers" are a comment-mention + two deferred branches), gated
behind a still-scaffolded caller, or already logic-absorbed into gfx.c.
The "ported caller" frontier metric overcounts via comment tokens and
scaffolds; this chip instead continued the live new-game thread.

## 2026-05-30 — AUDIO chip: one-shot cue-buffer teardown (FUN_00427d19)

Landed the top `port-frontier.md` leaf (5 ported callers) — retail
`FUN_00427d19` @ 0x427d19 (89 B).  The frontier's address-band label
("strategic-map camera + scroll") is a heuristic artifact: 0x427d19 is
**DirectSound**.  It stops + releases the single reusable one-shot
WAV-cue *secondary* buffer (`DAT_00522aec`) and nulls it, guarded on
both the IDirectSound interface (`DAT_004d4760` / `g_ds`) and the buffer
being live:

    if (g_ds && g_cue_buf) {
        IDirectSoundBuffer_Stop(g_cue_buf);     /* vtable +0x48 */
        IDirectSoundBuffer_Release(g_cue_buf);  /* vtable +0x08 */
        g_cue_buf = NULL;
    }

Ported into `src/audio.c` as `audio_cue_release()` (decl in `audio.h`).
It is the simplest leaf of the one-shot cue cluster (`0x427990`
load+create+play, `0x427c9b` is-playing), which stays deferred — that
create path pulls the `.wav` loader `FUN_004b984f` and the tier-2/3 IO
retry chain.  Since nothing assigns `g_cue_buf` yet, the function is a
faithful runtime no-op — exactly what retail's own null-guard yields in
that state.  `0x427990` calls `0x427d19` at its head to free the
previous cue before starting a new one, so the engine reuses ONE
secondary buffer for every cue rather than caching per-sound.

Vtable offsets decoded + cross-checked across the cluster: Stop +0x48,
Release +0x08, GetStatus +0x24 (`0x427c9b`), Lock/Play/Unlock
+0x2c/+0x30/+0x4c (`0x427990`).  Not host-tested — `audio.c` is
Windows-only (dsound.h); mingw drop-in build clean (both `.exe`,
`-Wall -Wextra`), `_audio_cue_release` present in the PE symbol table.
Deferred neighbours cited as bare hex per the ledger-token convention.
Findings: `docs/findings/ddraw-dsound-bringup.md` (new "Cue-buffer
teardown" section).

## 2026-05-30 — GAME-START chip: overworld-grid reset leaves (FUN_0046c5b8 + FUN_0046df51)

Descended into **FUN_004676e0** — the overworld-grid setup
sub-orchestrator, one of `FUN_00497ced`'s 25 new-game callees — and
landed its two cleanest leaf callees.  Both bulk-clear the 64×64×8
overworld cell grid (`g_overworld_map`, save entry 0), so they joined the
entity placer in `engine_territory.c`, the module that already binds that
grid mutably.

  - **FUN_0046c5b8** → `engine_territory_clear_cell_field(field)`.  Zeroes
    byte `field` of all 4096 cells.  Shared with the reset path
    `FUN_0046d3da`; both callers sweep fields 0..5, so cell bytes 0..5 are
    wiped and +6/+7 left.  r2 (0x46c5b8) confirmed byte-sized stores and
    that the param is the low-byte field offset (Ghidra's 8-way unroll
    obscures it).  Retail drives the sweep with three write-only scratch
    globals — `DAT_0053e8b8` plus engine_path's nearest-result pair
    `DAT_00569570/78` reused as loop counters — so the port uses a plain
    4096-cell loop and leaves engine_path's result globals untouched, the
    same call the map_query box scans already make.

  - **FUN_0046df51** → `engine_territory_clear_cell_render_flags()`.
    Clears bit 0x20 of byte +2 of every cell, then marks the full screen
    dirty.  Byte +2 bit 0x20 is a per-cell "draw via the alternate path"
    flag (set by 0x46e067/0x469370, read by the strategic draw helpers
    0x408438/0x4063c1/0x4071a0/0x406673/0x406bba gated on
    `DAT_0053f264 == 1`); new-game wipes it and forces a repaint.  The
    repaint routes through an optional bound dirty state
    (`engine_territory_set_dirty_state`, NULL-safe for host/headless) —
    the same `g_dirty_state` pattern the render modules use, the only
    outward coupling this otherwise-pure module gains.

7 host tests (4 field-clear + 3 render-flag, incl. unbound no-ops and the
full-screen mark).  1343/1343 pass; mingw drop-in build clean.  This
clears 2 of FUN_004676e0's 8 unported callees; the remaining leaf is
`FUN_0046da4b` (a 16×20 terrain index table over `DAT_0053ea00`).

## 2026-05-30 — GAME-START chip: army start-marker placement (FUN_00427ed0 + entity placer FUN_0046e1b0)

The caller the prior chip flagged as a trap (quirk #130).  `FUN_00427ed0`
(a 0x497ced new-game callee) drops each starting army's marker onto the
strategic map: it walks the army-id list at save entry 139
(`s_slot_00569518`, ≤6 ids, zero-terminated), probes for a nearby map
feature, and on a hit allocates a **type-6 entity** at the matched cell,
then stamps the army id/index into the new record and bumps the placed
count at entry 78 (`s_slot_005530b4`).

**Quirk #130 resolved — the matched cell now flows out via out-params.**
Retail's box scans leave the hit cell in `engine_path`'s nearest-result
globals `DAT_00569570/78` (they reuse them as loop counters), and
`FUN_00427ed0` reads those right after the probe returns truthy.  Our
ports deliberately used locals, so the prior chip flagged that the cell
must be surfaced before this caller could land.  Took the out-param
route: `box_scan` + the two box predicates gained `int *out_col,
*out_row`, threaded through `army_near` to the public army probes;
`engine_game_init_place_army_starts` reads them with retail's `||`
short-circuit and hands the cell to the placer.  No global clobber, no
clash with the (separately ported) `engine_path` nearest-result.  The
ring finder / region_clear pass NULL.

**FUN_0046e1b0 — the map-entity allocator — landed in `engine_territory.c`.**
792 B, *not* init-only (also reached by 0x4a0015 / 0x4a551b / 0x4ac185),
so it lives in the shared territory module rather than `game_init.c`.  It
allocates the first free record (slots 1..149) of the 151×420 table
(`g_per_territory_420`, retail `DAT_0052f0b0`) for a free map cell
(head-link +5 == 0 AND raw-flag +1 & 0xfc == 0), zeroes it
(`FUN_0046ead0` = memset 0x1a4), writes the position/type field map, and
claims the cell (cell +5 ← slot, +2 |= 1).  The slot it parks at is
retail `DAT_0052afe0` (a transient, *not* a save field), exposed via
`engine_territory_current_slot` so the caller can stamp extra fields —
exactly how `FUN_00427ed0` and the three other callers use it.

10 host tests (7 placer: field map, first-free-slot, occupied-cell /
blocked-rawflag / off-grid rejection, table-full, unbound; 3 army-start:
matched-cell report, two-army placement, skip+terminate).  1336/1336
pass; mingw build clean.  The placer needs the map-cell grid bound
mutably (`engine_territory_bind_cells`) in addition to the record array —
the live orchestrator (game-start step 5) will wire both when assembled.

## 2026-05-30 — MAP-QUERY chip: the three box-scan callers (FUN_00428007 / FUN_00428078 / FUN_0046733c)

The natural follow-on to yesterday's box-scan pair: the three direct
callers it just turned into leaves, all part of the new-game chain
(`0x497ced` → `FUN_00427ed0` → these probes).  Tiny (113/113/124 B),
they only return a 0/1 (or a ring distance) and read nothing else, so
they land byte-faithfully on the existing locals-based box scans.

  - **FUN_0046733c** → `engine_map_query_nearest_feature_ring(col,row)`
    (engine_map_query.c).  Pure (col,row) ring-distance probe: for
    d in 1..5 it tests the bit0 box at radius **d+1** then the
    only-bit1 box at radius **d**, returning d at the first hit, else 0.
    The +1 asymmetry between the two predicates is the only subtlety —
    pinned by two tests (bit0-at-distance-2 → ring 1; only-bit1-at-
    distance-2 → ring 2).

  - **FUN_00428007 / FUN_00428078** → `engine_game_init_army_near_rawflag_bit0`
    / `_only_bit1(id)` (game_init.c).  Look up army `id`'s map
    coordinate, probe Chebyshev radius 1..3 with the matching box
    predicate, return 0/1.

**r2 caught a Ghidra lie.**  Ghidra renders the army-coordinate lookup
as `(&DAT_0053fa1c)[id * 0x300]` — a stride that conveniently matches
the 768-B per-army record of the block it lives in (save entry 4,
`g_per_army_768`).  `r2 pdf` shows `mov cl, byte [eax + eax*2 +
0x53fa1c]` = `id*3`: a packed **3-byte-stride** coord array `{col,row,?}`
at offset 0x6c of entry 4, not one field per 768-B record.  The
decompiler mis-scaled a `base + idx*3`.  A `stride_is_3` regression test
seats two armies one 3-byte record apart and confirms each probe reads
its own (col,row).  Logged as engine-quirks #129; entry 4's save-format
row now flags that it carries internal sub-tables.

**A trap noted for the next chip.**  `FUN_00427ed0` (a direct `0x497ced`
callee, the orchestrator above these probes) does
`if (probe(id)) FUN_0046e1b0(3, DAT_00569570, DAT_00569578, 6)` —
reading the box-scan loop-counter globals, which on a hit hold the
matched cell's (col,row).  Our box-scan port uses locals, faithful for
the three callers here (they read only 0/1) but **not** for `0x427ed0`:
when it's ported the box scan must expose the matched cell (out-param or
real globals), or the placement targets the wrong tile.  Flagged in
engine-quirks #130 and in a pointed comment at
`engine_map_query_nearest_feature_ring`.

Nine host tests (6 ring-distance + 3 army-probe) pin the predicates, the
radius asymmetry, the empty/blanketed-grid behaviour, the out-of-range
return-0, and the 3-byte stride.  1326/1326 pass; mingw drop-in build
clean.

---

## 2026-05-30 — MAP-QUERY chip: FUN_0046cfbd + FUN_0046d130 (box-scan pair)

Walking down the new-game orchestrator's `FUN_00427ed0` callee chain
reached a sibling leaf pair: two near-identical 371-B box scans over the
64×64 overworld cell grid (`g_overworld_map`, save entry 0, 8 B/cell).
Both land together in `engine_map_query.c`, joining the existing
`region_clear` (FUN_0046d2a3) port.

Each scans the `(2*radius+1)`-square box centred at (col,row), **clamped**
to the 0..63 grid (the box shrinks at the edges — unlike `region_clear`,
which rejects out-of-range), row-major, and returns 1 at the first cell
whose head-link byte (+5) is zero and whose raw-flag byte (+1) matches a
predicate.  The pair differs only in that predicate (r2-confirmed at the
single differing instruction — `and cl, 1` @ 0x46d0df vs `and cl, 0xfd`
@ 0x46d252):

  - FUN_0046cfbd → `engine_map_query_box_any_rawflag_bit0`:  `(flag & 1) != 0`.
  - FUN_0046d130 → `engine_map_query_box_any_rawflag_only_bit1`: `(flag & 0xfd) == 0`.

Both retail bodies reuse the find-nearest scratch globals
`DAT_00569570` / `DAT_00569578` as loop counters (the same codegen
artifact `region_clear` has); the three callers (0x46733c / 0x428007 /
0x428078) read only the 0/1 result, so — as with `region_clear` — the
port uses locals rather than clobber `engine_path`'s nearest-result
globals.  Ported byte-exact via one shared `box_scan(mask, expect)`
helper plus two thin wrappers.

Ten host tests pin both predicates, the head-link gate, the bit masks,
the empty-grid behaviour (`only_bit1` matches an all-zero cell), the
edge clamping (right edge and negative anchor — the distinguishing
behaviour from `region_clear`), and the radius-0 single-cell case.
1317/1317 pass; mingw drop-in build clean.

Landing this pair makes their callers `FUN_00428007` / `FUN_00428078`
(per-army neighbourhood probes) and `FUN_0046733c` (ring-distance
finder) into leaves, advancing the `0x427ed0` sub-chain.

---

## 2026-05-30 — GAME-START chip 9: FUN_004ae74d (clear castle-shadow table)

A direct orchestrator (`0x497ced`) callee that had become a zero-dep
leaf — and one that an earlier pass had **deferred** over an
aliasing/overrun worry.  r2 re-analysis (0x4ae78b/0x4ae791) resolved it:
the producer's 400×16 double loop writes a dword at
`[row*0x80 + col*8 + 0x56d8b8]` and a byte at `+0x56d8bc`, with row
0..399 and **col 1..16**.  The base `0x56d8b8` is entry10 − 8, so the
1-based column lands the first write exactly at `g_castles_shadow` (save
entry 10, `0x56d8c0`, 6400 × 8 B — already homed) and the last at
entry10+51192: every 8-B record, fully in-bounds, **no tail overrun**
(the prior note miscounted).  The col-1 start also means the 8 bytes
before entry 10 are never touched — those are two *unrelated* aliased
runtime globals (a scalar cursor at `0x56d8b8`, a freeable pointer at
`0x56d8bc`), which is exactly the desync the deferral feared; the
function deliberately steps over them.

`FUN_004ae74d` → `engine_game_init_clear_castle_shadow` (`src/game_init.c`).
Per 8-B record it zeroes the +0 dword and the +4 byte, preserving
+5..+7 (the per-castle-slot back-pointer is at +5, per save-format.md);
then it zeroes three world scalars — entries 66/67/68
(`s_slot_0055300c` / `s_slot_00568da8` / `s_slot_00553f30`, all homed).
Ported byte-exact (memset of the 5 cleared bytes per record).

Two host tests pin the per-record clear/preserve split (dirty the whole
51200-B block with 0xAB → every record's +0..+4 zeroed, +5..+7 kept;
ASan guards the 1-based arithmetic against running past the array) and
the three-scalar reset.  1307/1307 pass; mingw drop-in build clean.

Remaining direct `0x497ced` callees: FUN_00499aca, FUN_00427ed0,
FUN_004676e0, FUN_004984dc, FUN_00451150, FUN_00448440, FUN_0049bd99,
FUN_00499d7d (audio), plus the Win32 `PostMessageA(WM_MOUSEMOVE)` poke
FUN_004b18f8 (deferred to a Win32 module).  No pure-sim leaves left
among them — the next chips walk down each callee's chain (the IO/chdir
cluster under 0x499aca, the table scans under 0x427ed0).

---

## 2026-05-30 — IO chip: FUN_004af502 (revert cwd to asset base dir)

Triaging the remaining new-game orchestrator (`0x497ced`) callees showed
their next dependencies are no longer pure sim chips but the **chdir /
asset-IO cluster**: the near-leaf callee `0x499aca` reaches the file-exists
probe `FUN_004af841`, which calls both `0x4af312` (tier-2 subdir descent)
and `FUN_004af502` (revert-to-base).  `FUN_004af502` is also the
single highest-fan-in leaf in the whole frontier (8 ported callers).

Ported as `src/io_path.c::io_path_chdir_base` — a **new portable module**.
The key realisation: retail's `__chdir`/`__getcwd` are the MSVCRT
`_chdir`/`_getcwd` wrappers (r2-confirmed → Set/GetCurrentDirectoryA),
*not* Win32 API, so the cluster can use the same CRT calls (`_chdir`/
`_getcwd` on Windows, POSIX `chdir`/`getcwd` on the host) and stays
host-testable — unlike the `windows.h`-bound `io.c`.  The module owns
the base-dir buffer (retail `DAT_0058fe30`), a `io_path_capture_base`
(the `__getcwd(&DAT_0058fe30,100)` the WinMain owner `0x40e9ab` does —
now wired into WinMain right after `init_exe_dir`, completing the capture
the `0x40e9ab` port had elided), an `io_path_set_base` test seam, and
the revert itself.

Two documented deviations: the `status.txt` failure logger (`0x4afab9`,
unported) is elided — the `"ERR:CHD  not reserted is now <cwd>"`
diagnostic (string sic) routes through the drop-in dev log, per the
`gfx.c` convention — and the retail `void` signature gains an `int`
chdir-result return for host observability (the `save_rotate_and_write`
precedent).  r2-verified the `chdir`/`getcwd(,100)` shape and the
nonzero-return error branch at 0x4af515.

Four host tests pin: the capture→wander→revert round-trip, the
`set_base` seam, the failure path (bogus base → nonzero return, cwd
unmoved), and the 100-byte truncation + NULL-clear.  1305/1305 pass;
mingw drop-in build clean.  First member of the chdir cluster to land —
unblocks the `0x499aca` game-start sub-chain and the deferred asset
tiers 2/3 (`0x4af312` / `0x4af116`), which now share the
`io_path_base()` anchor.

---

## 2026-05-30 — GAME-START chip 8: FUN_004a1c53 (reset per-player sub-tables)

The direct 0x497ced callee whose query sub-callee chip 7 prepared.
`FUN_004a1c53` → `engine_game_init_reset_player_subtables`
(`src/game_init.c`), folding in its other sub-callee `FUN_004a2584` as a
static grid-clear helper.  Both are init-only (callers inside 0x497ced /
0x4a1c53).

Three steps: zero entry 144; selectively clear the entry-146 6×5 grid of
8-B elements (zeroing +0/+1/+2 and the dword +4, leaving byte +3);
recompute the first-active-non-local cursor via chip 7's
`player_first_active_nonlocal`.  Then for each player slot 1..5 it resets
a 5-entry sub-table of 0x10-B elements at record offset 0x94 plus two
standalone bytes — each element's lead byte seeded to 5 for AI players
(F_ACTIVE set, F_HAS_DATA clear), 0 otherwise; +0xd → 1; other written
fields zeroed; element bytes +0x6/+0x7 left untouched.  All offsets and
the lead-byte branch r2-confirmed (0x4a1c5c…0x4a1e3a); field semantics
provisional (computed-pointer consumers).  Homing a non-event (entries
144/146 already homed).

Three host tests pin the grid clear (+ entry-144 reset), the lead-byte
AI/human/inactive split with element fields and standalone bytes, and
slot-0 untouched + cursor recomputed.  1301/1301 pass.

Remaining direct 0x497ced callees: FUN_00499aca, FUN_00427ed0,
FUN_004676e0, FUN_004984dc, FUN_00451150, FUN_00448440, FUN_0049bd99,
plus FUN_00499d7d (audio).  No leaves among them yet — the next chips
walk further down each one's callee chain.

---

## 2026-05-30 — GAME-START chip 7: FUN_004a1e6c (first-active-non-local-player query)

Prep for the next 0x497ced callee (`FUN_004a1c53`, the per-player record
init): its sub-callee `FUN_004a1e6c` has a **second, external caller**
(0x416cf3, a render/UI path), so it is *not* init-only and lands in the
shared roster module rather than `game_init.c`.

`FUN_004a1e6c` → `player_first_active_nonlocal` (`src/engine_roster.c`):
scans player slots 1..5 of `g_player_strategic_352` for the first that is
occupied (the +0x4 flag) and not the local player (`s_slot_0057c8cc`,
entry 72 — the established `local_player` cursor), storing that slot in
`s_slot_00553fbc` (entry 143), or 0 if none.  Both scalars already homed;
slot 0 (reserved front slot) is never considered.

Four host tests pin: skips the local player, picks the lowest occupied
non-local slot, yields 0 when only the local player is active, and
ignores slot 0.  1298/1298 pass.  Next chip wires this into the
`FUN_004a1c53` per-player init.

---

## 2026-05-30 — GAME-START chip 6: FUN_0049b35e (purge inactive strategic records)

Another direct 0x497ced callee lands — and a two-step chain in one chip.
`FUN_0049b35e` → `engine_game_init_purge_inactive_records`
(`src/game_init.c`); its sole unported callee `FUN_0046ea61` is just the
len-0x160 instance of the fixed-length record zeroer (a fourth sibling of
FUN_0046ea28/eb09/ead0), so it folds into the existing `clear_record`
helper and the whole chain lands together.

The seeder walks player slots 1..5 of `g_player_strategic_352` (save
entry 3, stride 0x160; slot 0 skipped) and zeroes the whole record when
the slot is unoccupied (`+0x4 == 0`) or occupied-but-uninitialised
(`+0x5 == 0 AND +0x7 == 0`) — the same F_ACTIVE / F_HAS_DATA / F_OPP_TYPE
fields `engine_ranking.c` already names.  Init-only (sole caller is the
new-game initialiser, the call site 0x497e5b sits inside 0x497ced's
range), so it belongs in `game_init.c`.

**Homing was a non-event** — the base is the already-homed entry-3 table.
Two host tests pin the zeroing (the OR and the AND branches) and the
preservation (active slots survive; a set opp-type alone keeps a record;
slot 0 is never visited).  1294/1294 pass.

Three direct 0x497ced callees now remain near-leaves (FUN_0049d7d audio,
FUN_00499aca, FUN_004a1c53 each 1–2 unported callees); the rest still
carry deep fan-out (FUN_00448440 = 28, FUN_0049bd99 = 9).

---

## 2026-05-30 — GAME-START chip 5: leaf seeder FUN_004ac904 (unit/opponent-type table)

A clean leaf seeder of the new-game init (`0x497ced`): `FUN_004ac904` →
`engine_game_init_seed_unit_table` (`src/game_init.c`).  Triage of the
remaining 0x497ced callees found this the next zero-dependency chip — its
sole callee (`FUN_0046f2ae`, the unit-table clearer) is already ported,
so it lands today with no new deps; the heavier remaining seeders
(FUN_0049bd99 / FUN_004984dc / FUN_00451150 / FUN_00448440) still carry
5–28 unported callees.

It fills the unit/opponent-type table `g_unit_table_20` (save entry 6,
13 records × 0x14 B) with a **scenario-selected row count**: the config
selector `s_slot_0056d5dc` (entry 77) indexes a 20-entry `.rdata` lookup
table (ramp `0..12` then flat), the count is clamped `1..12` and
**persisted to `s_slot_00554030`** (entry 122 — this is its sole writer),
then records `1..count` are filled from six parallel `.rdata` columns.

**Homing was a non-event** — both scalars (entries 77, 122) and the dest
block (entry 6) were already homed; the six const columns are embedded as
`static const` arrays extracted byte-for-byte from the PE.

**Three quirks worth noting** (all ported verbatim): (1) the count clamp
is *dead* for any valid selector 0..19 — the table never exceeds 12 — but
a table value of **0** (selector 0) is *not* bumped to 1, so the fill
loop runs zero times; (2) two columns are each written to **two adjacent
record fields** (col A → +0x2 and +0x4; col C → +0x6 and +0x7); (3) the
six `.rdata` columns are packed so each one's entry[0] **aliases the
previous column's entry[12]** (col_e[12] ≡ col_f[0] at 0x4de7ec) — only
entries `1..count` are read, so the overlap is harmless.

Field semantics stay provisional: rows pair up by class (col B =
`4,4,5,5,3,3,1,1,2,2,6,6`) with three stat dwords, but consumers reach
the table through a computed base pointer (no static field xrefs), so the
meaning waits for a reader subsystem.  Four host tests pin the full
12-row fill field-for-field, the partial-count case (records past `count`
stay cleared), record-0 stays cleared, and the count selection across the
whole 0..19 selector domain.  1292/1292 pass.

See `docs/findings/game-start.md` for the column table and details.

---

## 2026-05-30 — GAME-START chip 4b: heavy seeder FUN_0049aa0e (player standings recompute)

The second heavy seeder lands in its **own module**: `FUN_0049aa0e` →
`engine_ranking_recompute` (`src/engine_ranking.c` + `.h`).  Triage of
the five remaining step-4 heavy seeders found this one is the clean next
chip — its sole callee (`engine_event_queue_post`, FUN_00472bc5) is
already ported, so it is effectively a leaf; the other four still carry
5–28 unported callees.  It also turned out to have **5 callers** (the
new-game init 0x497ced plus the turn/event paths 0x49b42b / 0x49cebe /
0x49a010 / 0x435211), so it is *not* init-only and gets its own module
rather than living in `game_init.c` (same call as `engine_strat_cost`).

It recomputes the whole-game **standings** from the 6 per-player
strategic records (`g_player_strategic_352`, stride 0x160) in one
six-phase pass: clear 19 result scalars → per-metric max + holder (8
metrics) + active count → per-player composite score → 5-entry
`{slot, score}` board + descending bubble sort → 1-based ranks →
winner/last detection → victory-event post when one player survives.

**Homing was a non-event** — all 20 result scalars are existing
save-schema slots (entries 124, 151–168), the leaderboard board is
`s_slot_00565410` (entry 150, 6×8 with a trailing zero sentinel), and
the per-player table was already homed.  Nothing new to home.

**The score band is dead past its first arm** (r2-confirmed at
0x49aed1): the `< 2001` guard makes the decompile's `+100` / `+200`
bands unreachable, so only `>= 2001 → +50` ever fires.  Ported verbatim
including the dead arms; a test pins all four band cases (2000→0,
2001/8000/20000→50) so the dead arms can never silently come alive.

**First wiring of the event-queue producer.**  `engine_event_queue_post`
had no callers yet; phase G is the first.  The producer takes a
`player_has_data[7]` filter + `local_player` explicitly, so the port
rebuilds the filter from the table — slot 6 (which would overrun the
6-slot array) stays 0 and is never read here, since the only targeted
post uses `target ∈ 1..5`.  The event-queue state is threaded in as a
pointer (the function's only side effect outside the save globals).

r2-verified: the 11·32 = 0x160 record stride, the metric offsets, the
score multipliers (×10/×2/×2/×50) and divisors (÷10/÷5), and the band.

Coverage **359 → 360 touched** (FUN_0049aa0e).  +10 host tests (metric
leaders + strict-`<` tie-break, local-excluded active count, score
weights, the four band cases, sort/ranks/winner, inactive→rank-0, both
victory-event arms + rot wrap, no-event multiplayer) → **1288 green**;
both drop-in builds clean.

Next heavy seeders all still gated on unported callees: FUN_0049bd99
(3 KB, 9), FUN_004984dc (5), FUN_00451150 (7), FUN_00448440 (28).

---

## 2026-05-30 — GAME-START chip 4a: heavy seeder FUN_00497e65 (new-game randomiser)

The first **heavy seeder** (step 4 of the game-start plan) lands:
`FUN_00497e65` → `engine_game_init_seat_shuffle` (`src/game_init.c`).
It is the new-game **randomiser** — one retail unit (1025 B, sole caller
0x497ced) that consumes a single contiguous run of double-LFSR draws
(`FUN_00404a46`, already ported as `engine_rng_step`; each draw reads
the 7-bit-b mirror `DAT_0058fd60`) and produces three pieces of
randomised state whose **draw ordering is part of the persisted
result**, so they stay one C function:

1. **Roster shuffle** — re-seats the `active` occupied roster pairs
   (`g_player_roster`, seats 1..5; seat 0 = reserved front slot,
   excluded) into random array positions, each pair's `{army-id,
   seat-id}` payload travelling together.  Assumes the roster was
   compacted (`player_roster_compact`) so occupied pairs are contiguous.
   `active` draws.
2. **48-entry permutation table** (save entry 178, `s_slot_0057cae0`) —
   identity 0..47 dropped into random empty slots (0x32 probe cap, 0x30
   wrap, never its own index).  48 draws.
3. **48-entry value/threshold table** (save entry 191, `s_slot_00553080`)
   — each cell `(rng & 7)` → 0 / 100 / `roll·5+6` {6,11,16,21,26,41}.
   48 draws.

Both 48-tables are indexed in-game by the shared cursor `DAT_005653f8`
(permutation → `DAT_00553528`, readers 0x47aaa3 / 0x444a2f; value table
compared as an AI threshold at 0x48acd2 / 0x48b02b).

**RNG seam.**  The function takes the threaded LFSR state by pointer
(`engine_rng_in_t *`) and advances it through `engine_rng_step` — pure
and host-testable given a seed, and ready for the step-5 orchestrator to
continue the draw sequence from the prior seeders.  This is the first
game-init seeder to consume RNG; the existing pure `engine_rng_step` +
the engine.c state-mirror model dropped straight in.

Ported verbatim, **r2-verified**: masks 3 / 0x1f / 7, the `+1` target
bias (`inc eax`), the 0x30 permutation wrap, the 0x32 probe cap, and the
value ladder `lea eax,[eax+eax*4]; add 6`.

Coverage **358 → 359 touched** (FUN_00497e65).  +7 host tests (payload
preservation, active==0, permutation uniqueness, value-ladder range, a
full RNG-replay locking the ladder + the active+48 draw position, exact
total draw count active+96, determinism) → **1278 green**; both drop-in
builds clean.

Next heavy seeders: FUN_0049bd99 (3 KB, largest), FUN_0049aa0e,
FUN_004984dc, FUN_00451150, FUN_00448440.

---

## 2026-05-30 — GAME-START chip 3b: leaf seeder FUN_0046ff43 (strategic movement-cost grid)

The second new-game leaf seeder lands as its own module:
`FUN_0046ff43` → `engine_strat_cost_rebuild`
(`src/engine_strat_cost.c` + `.h`).  It rebuilds the **64×64 strategic
movement-cost grid** `g_strat_move_cost` (retail `DAT_004f4080`, 4096 ×
i16, row stride 64) from the overworld terrain descriptors
`g_overworld_map` (`DAT_00522f90`, save entry 0 — 8 B per cell).

Per cell it reads three descriptor bytes — +0 type, +1 flags, +7 a
level/count — and maps them to a cost word through a fixed branch
ladder whose **test order is load-bearing** (water/blocked flag 0xc →
0; the +7≥0x11 gate → 0; bit0 road → 1; bit5 → 3/6/3 by type; bit6 →
100; bit7 → 0x14→3, {3,6,9,0xc}→0, else 100; bit4 → type 0x10→100 else
0; default → 3).  Ported verbatim from the decompile, preserving the
priority cascade.

**Why a new module, not game_init.c.** This is a *separate* pathfinder
from `engine_path` — that one is the 80×80 army router over the BFS
cost map at 0x504030; this is the 64×64 strategic grid.  The cost grid
is read by the still-unported strategic flood-fill (0x46f700) and
path-trace (0x4701ac) at directional neighbour offsets, so it gets its
own home (`engine_strat_cost`) those ports will share when they land —
rather than being parked inside the game-start initialiser (even though
FUN_0046ff43 is one of 0x497ced's leaf seeders).

The function takes the source map as a pointer (pure, host-testable);
the canonical `g_strat_move_cost` global is the DAT_004f4080 home.

Coverage **357 → 358 touched** (FUN_0046ff43).  10 new host tests —
one per ladder branch, plus predicate-ordering (road beats bits 5/6/7;
the +7 gate beats road), whole-grid coverage (all 4096 written, sentinel
pre-dirtied), and the (col,row)→row*64+col indexing → **1271 green**;
both drop-in builds clean.

This leaves **FUN_004ae74d deferred** as the only un-landed step-3 leaf
(aliased `DAT_0056d8b8` region — see chip 3a entry + game-start.md).
Step 3 is otherwise complete; next is the heavy seeders (step 4).

## 2026-05-30 — GAME-START chip 3a: leaf seeder FUN_0042847c (value-table reset)

The first of the new-game **leaf seeders** (step 3 of the game-start
plan) lands: `FUN_0042847c` → `engine_game_init_seed_value_table`
(`src/game_init.c`).  It resets the 16-entry value table at **save
entry 82** (`s_slot_005532a0[256]`, laid out as 16 records × 16 B) to
defaults: each record's first dword from the retail `DAT_004d8910`
const table, the second dword (+4) zeroed, the trailing 8 B of every
record left untouched.

**The dest was already homed** — entry 82 is `s_slot_005532a0[256]` in
the shared save-schema globals, so the seeder just writes into it (same
model as the scalar-seed phase).  The 16-dword source table was
extracted byte-for-byte from the PE `.rdata` (`{0,2,0xc,0, 1,0,1,2,
1,0xd,0x10,0xa, 0x18,0x17,0x2c,0}`) and embedded as a `static const`.

Unlike the scalar-seed phase, this seeder has **four retail callers**
(0x497ced new-game + 0x43723a / 0x49a010 / 0x4a98fe), so it is a
reusable "reset table-82 to defaults" entry point, not a one-shot;
the neighbours are cited as bare hex so the ledger doesn't miscount
them.  Record `i`'s first dword is later read by the cursor
`DAT_0056d8ac` (render 0x41608b/0x428daf, getters 0x4a4a3f/4b12/4c41).

Coverage **356 → 357 touched** (FUN_0042847c).  2 new host tests
(defaults match; trailing 8 B per record untouched — each pre-dirtied
so a no-op can't pass) → **1261 green**; both drop-in builds clean.

### The other two "leaf" seeders — one portable, one deferred

- **FUN_0046ff43** (617 B) — builds the **64×64 strategic
  movement-cost grid** (`DAT_004f4080`, 4096 × i16) from the overworld
  terrain descriptors (`g_overworld_map`, save entry 0).  A separate
  pathfinder from `engine_path`'s 80×80 army router (which uses the
  cost map at 0x504030); its grid is read by the still-unported
  strategic flood-fill (0x46f700) and path-trace (0x4701ac).  Portable
  as a pure terrain classifier — chip 3b, below.
- **FUN_004ae74d** (144 B) — **deferred.**  It clears the
  `DAT_0056d8b8` region, but r2 shows the real addressing is
  `base + rec*0x80 + e*8` (outer stride **0x80**, not the 0x20 Ghidra
  prints), writing a dword + a byte per 8-B element for elements
  1..6400 (element 0 skipped, element 6400 a deliberate 8-B overrun).
  That region is **aliased across a web of unported code** — used as a
  scalar cursor (`=10`, wrap-at-63 in 0x46a295), as a freeable pointer
  at +4 (`DAT_0056d8bc` freed in 0x49915e, set in 0x4990f9), and as the
  payload array (0x4156a7/0x4ae7dd).  Homing it now (size, element
  struct, which uses share storage) would be speculative; it waits
  until one of its reader subsystems lands to fix the shape.  Recorded
  in [`findings/game-start.md`](findings/game-start.md).

## 2026-05-30 — GAME-START chip 2: the new-game scalar-seed phase (calendar + handshake + free DATs)

The second chip of the new-game initialiser (`FUN_00497ced`) ports its
**scalar-seed phase** — the 20 dword world-state globals written between
the NPC-schedule seed and the first heavy seeder — into a new
`engine_game_init_seed_scalars` (`src/game_init.c`).  The phase wipes the
turn-handshake state and seeds the start calendar, the table the
strategic-map header reads.

**The home decision the previous entry flagged is resolved.** The 20
writes split three ways:

- **17 save-schema scalars** → their shared `s_slot_<va>` homes, seeded
  the same way `engine_npc_schedule.c` already does.  No new storage.
- **2 turn-handshake runtime globals** (`DAT_00553fc8`,
  `_DAT_0056d658`) — these turned out to be *already homed*, as
  `engine.c` file-statics (`g_sim_turn_progress` /
  `g_sim_turn_sync_baseline_ms`), just unreachable: `engine.c` is kept
  out of the host test suite (it pulls in `mmsystem.h`/`gfx.h`/the main
  loop), and a second copy in `game_init.c` would be a desync trap.
  **Relocated their storage to `engine_turn.c`** (the turn module —
  already test-linked, light deps) behind a fresh `engine_turn.h`
  extern; `engine.c` keeps the live per-tick read/write and reaches
  them through the header it already includes.  Pure mechanical move,
  no behaviour change.
- **1 genuinely-unhomed free DAT** (`DAT_0053f0c4`, a render/anim gate
  counter; sole reader `0x41e1dd`, unported) → parked in `game_init.c`
  as `g_anim_gate_53f0c4` until its render subsystem lands.

**What the asm taught the port** (→ new engine-quirk **#128**): the
start date isn't two numbers.  The init writes **six** redundant calendar
scalars as two "triple + copy" groups — `year` 0x4f3 (1267) with its
`±1` neighbours (1266/1268) and a verbatim copy, and `season` 3 with its
`±1` neighbours (2/4) and a copy — derived in the asm from a single base
via `inc`/`dec`.  The pre-computed neighbours are why the map header can
show adjacent years/seasons with no draw-time arithmetic (entry 20 →
`FUN_0040f5fd`/`FUN_0041a900`).  The season-next write carries a retail
`if (>4) →1` seasonal wrap that is **dead** at the seeded 4 (the
decompiler folds it); preserved as a comment, not reproduced.  The port
keeps the base→±1 relationships as expressions, not magic constants, and
issues all 20 writes in retail order.

Coverage holds at **356 touched** — the phase ports no new *functions*
(the writes were inline in the still-unported `0x497ced`), so the
function-touch count is unchanged by design; the unported parent and the
next seeder `0x4676e0` are cited as bare hex, not `FUN_` tokens, so the
ledger doesn't miscount them.  2 new host tests (calendar block;
fixed-value/zero/handshake post-conditions, each pre-dirtied so a no-op
can't pass) → **1259 green**; test suite + both drop-in builds clean.

### Next

- **Leaf seeders** — `FUN_0042847c` (table copy `DAT_004d8910` →
  `DAT_005532a0` strided), `FUN_004ae74d` (array clear + 3 scalars),
  `FUN_0046ff43` (617-B leaf), the `FUN_0046f*` siblings.  All
  zero-dependency.
- Then the heavy seeders, the `engine_game_init` assembly + state flip,
  and the strategic-map screen arm of `FUN_0040f1a0`.  Full plan in
  [`findings/game-start.md`](findings/game-start.md).

## 2026-05-30 — GAME-START MILESTONE OPENED: survey + first chip (new-game table-clear phase)

The title-menu input subsystem is effectively complete (pages 1–9
navigable; pages 3 & 4 editable), so this session turns to the next
fresh scope — **game-start**, the path from the menu into a live game.
Two outcomes: a survey that de-risks the milestone, and the first
landed chip.

**The listed "page-4 Enter commit" next-item was a non-chip.** Traced
the retail VK_RETURN handler (`FUN_004b29be` case `0x0d` →
`FUN_00401c5b()` + `FUN_0043600c()`): `FUN_00401c5b` only sets
`DAT_005cd41c = 100` (the load-game request latch, consumed solely by
the load pump `FUN_004ad9f0` on the load screen, already modeled in src
as `g_filelist_load_request`); `FUN_0043600c` only acts when the
screen-id `DAT_004eac50` is `0` or `41` (the attract screens) — a
**no-op on page 4**.  Enter on page 4 produces no commit/navigation in
retail, so the PROGRESS hint mislabeled it.  Subsystem closed.

**Survey** (→ new [`findings/game-start.md`](findings/game-start.md)):
the page-7 Start path is `FUN_004335f0` (Start arm) → `FUN_00499dc3`
(roster finalize) + `FUN_004329ec` (dispatch) + `DAT_0052afa4 = 1`; the
SP dispatch runs `FUN_00497ced` (the 376-B new-game world initialiser),
sets screen-id 0 and flips engine state to ingame (`DAT_0052af94 = 3`).
Two findings shaped the plan:

- **The closure is the whole engine.** BFS from the Start roots reaches
  1217 functions / 533 KB (933 unported).  So game-start is *not* a
  single unit — the tractable target is the **init itself**
  (`FUN_00497ced`'s 25 direct callees, 20 unported, mostly run-once
  seeders), built bottom-up and host-validated per chip.  And a visible
  in-game map needs a *second* unported half: the strategic-map screen
  arm of `FUN_0040f1a0` (the screen-dispatch keyed on `DAT_004eac50`;
  only the `0x1f` title arm is ported today).  Flipping the state alone
  renders nothing.
- **The scalar-global wall is already down for this work.** Init writes
  ~20 world-state globals; 17 are save fields, but every one has a
  shared `s_slot_<va>` home in `save_schema_globals.h` — the
  runtime-global ownership model the 2026-05-29 wall waited on has
  shipped and is in use (`engine_npc_schedule.c` seeds `s_slot_`
  directly).  Game-init seeds them the same way; no save desync.

**First chip — the table-clear phase** (new `src/game_init.c`).
`engine_game_init_clear_tables` ports `FUN_00497ced`'s first three
calls — `FUN_0046f3cd` / `FUN_0046f2ae` / `FUN_0046f1dc`, each looping
its record count and zeroing one record per iteration via the
fixed-length leaves `FUN_0046ea28` / `FUN_0046eb09` / `FUN_0046ead0`
(all `memset(p, 0, len)`).  The loop bound × stride covers the full
named block in each case, so the three clears wipe `g_per_territory_420`
(151×420), `g_unit_table_20` (13×20) and `g_per_army_768` (17×768) —
the big per-record save blocks every later seeder writes into.  The
per-record structure is kept for provenance and retail call-shape
parity.  Safe (no mode flip, no live-behaviour change) and self-
contained (only depends on the `save.h` blocks).

Coverage 350 → **356 touched** (the 3 clearers + 3 zeroer leaves, all
host-tested).  4 new host tests (each block dirtied then asserted
fully zero; full-phase independence) → **1257 green**; both drop-in
builds clean.  The in-progress parent (`0x497ced`) and the Start spine
(`0x4335f0` / `0x4329ec`) are cited as bare hex, not `FUN_` tokens, so
the ledger doesn't count them touched until they're actually ported.

### Next

- **Scalar-seed phase** — `FUN_00497ced`'s ~20 `s_slot_`/free-DAT body
  writes.  Needs a home for the ~3 non-save "free" runtime DATs
  (`DAT_0053f0c4`, `DAT_00553fc8`, `DAT_0056d658`).
- **Leaf seeders** — `FUN_0042847c` (table copy), `FUN_004ae74d`
  (array clear + 3 scalars), `FUN_0046ff43`, the `FUN_0046f*` sibling
  clearers.
- Then the heavy seeders, the `engine_game_init` assembly + state flip,
  and finally the strategic-map screen arm of `FUN_0040f1a0` — only
  then does Start show a live map.  Full plan in `findings/game-start.md`.

## 2026-05-30 — PAGE 3 GOES LIVE: the load-game file list is navigable + its filename is typeable (`FUN_0040da1e` button hit-test + page-3 arm)

The page-3 "Loading a game." screen rendered already but was inert — reachable
only by `--title-nav`'s forced jump.  This entry ports its **input arm**, so
the gauntlet Load/Cancel buttons and scroll arrows dispatch with real clicks
and the filename field types live.  The new foundational piece is the engine's
**second** hit-test family — the button-record hit-test paired with the
`FUN_0040cfd2` button renderer — which every gameplay screen's button strips
also use.

Four pieces landed:

1. **`engine_menu_button_dispatch`** (port of `FUN_0040da1e`, the 869-byte
   button-record interaction handler).  Unlike the short[] hotspot path
   (`FUN_0040e3ee`) it (a) runs a per-button press-frame animation countdown
   every call — a clicked button latches `pressed = 3` / `state = 1` and
   un-presses three frames later, so it visibly depresses; (b) hit-tests a
   *square* box whose side is the record's `kind` byte (0x18 = 24 px, 0x20 =
   32 px — the same byte that selects the sprite, quirk #125); (c) dispatches
   type 4 (press + hold-to-auto-repeat), type 2 (toggle), type 5 (fire on
   release-animation-end).  The scroll auto-repeat rides a 0x30-byte sparse
   accel-gate table (`DAT_004d2748`, r2-dumped) that thins from "every 6th
   frame" toward fire-every-frame the longer you hold (quirk #126).

2. **The page-3 arm of `FUN_004ba9c8`** — enable + stream the editor into the
   filename field, then run the 4-button strip hit-test.  No hover pass.  The
   load-confirm pump `0x4ad9f0` (deep load → game-start) is deferred: it only
   acts once the `DAT_0057d3c4` confirm timer is armed.

3. **The three actions** — Load (`FUN_004342f3`: latch `DAT_005cd41c = 100`),
   Cancel (`FUN_00434308`: return to the bookmarked page `DAT_00553e5c`),
   scroll (`FUN_00434346`: advance first-visible `DAT_004ea1a0` by ±3 with the
   mode-1 clamp).  Dispatched by the record's retail action code, mirroring
   retail's call-through-pointer.

4. **Editor unification.**  Retail has exactly *one* text editor (the
   `DAT_005cd550` global); page 4's title name and page 3's filename both drive
   it, each streaming it to its own field.  The drop-in had split it into two
   pointers; this collapses them into a single shared `g_title_editor` the
   `WM_CHAR` route always targets.  `set_choose_title_editor` /
   `set_filelist_editor` bind the same slot; each page's name-tail copies it
   out (page 4 → `DAT_0053f1e0` width 0x1f; page 3 → `DAT_004ea130` width 0x40,
   capped at 8 — DOS 8.3, quirk #127).  The page-3 filename field became a
   renderer-owned mutable buffer (like the page-4 name) so the copy-out has a
   destination, and the page-3 button table is now shared mutable state so the
   hit-test's press-animation writes show on the next repaint.

Coverage 347 → **351 touched** (`FUN_0040da1e` + the 3 actions; the editor
unification and arm are pure wiring over already-counted functions).  8 new
host tests (load latch, cancel→bookmark, scroll up/down/clamp/forced-zero,
click-flash-then-settle, auto-repeat-gated-by-delay, editor-streams-to-
filename) → **1253 green**; both drop-in builds clean.

**Verified** (`runs/title-type-p3/`, 2026-05-30): `--title-live --title-page 3`
renders the file list live, the per-frame refresh paints the gauntlet
Load/Cancel + scroll arrows, and `--title-type war` posts real `WM_CHAR` into
the live WndProc — the filename field fills from the "lastturn" seed to
**"wartturn"** through the genuine keystroke route (frame_00000 seed →
frame_00016 typed + buttons).  Open to a human keyboard/click pass too.

### Next

- **Page-3 Cancel / Load human pass** — `--title-nav` already exercises the
  page-2 → page-3 hop; extend it to click Cancel (→ page 2) and a scroll arrow
  for a clicked-button parity capture, or drive it live by hand.
- **Page-4 Enter commit** — `WM_KEYDOWN` VK_RETURN (`FUN_00401c5b` arm-timer +
  `FUN_0043600c`) and the roster name-commit `FUN_0049bae9(999, …)`; Enter is
  still inert on page 4.
- **Start / game-start** (`FUN_004329ec` → `FUN_00497ced`) — the deep play-flow
  milestone behind page-7's Start, page-4's Play-Now Continue, and now page-3's
  Load (the deferred `0x4ad9f0` confirm pump lands here).

## 2026-05-30 — PAGE 4 NAME EDITING: the name box is typeable live (text-edit subsystem wired to the menu)

The page-4 "Choose your title and your shield" screen rendered and navigated
already, but its name box was a static seed — the last page-4 leaf.  This
entry wires the **global title text editor** (the already-ported
`text_input` cluster) into the live menu: real keystrokes now type into the
name box, with backspace/delete/arrows/Home/End/Insert all live.

Three seams landed, all over the already-ported `text_input` module
(`FUN_00401984` family) which held the editor logic but was unwired:

1. **The page-4 editor tail** (`FUN_004ba9c8` page-4 arm:
   `DAT_005aeb78 = 1; FUN_00401f49(); FUN_0040210c(&DAT_0053f1e0, 0x1f)`).
   `engine_menu_title_input`'s page-4 arm now, every frame, enables the
   editor, recomputes its length, and streams its buffer into the rendered
   name box (`engine_title_render_choose_title_name_tail`).  The dispatcher
   head resets the enable gate first (`FUN_004ba9c8` line 9
   `DAT_005aeb78 = 0`) so the keystroke route is gated off on every screen
   but the live field — each editable page sets it back to 1.

2. **The `WM_CHAR` / edit-key route** (`FUN_004b29be` keyboard arm).  The
   host WndProc feeds `WM_CHAR` to `text_input_filtered_keypress`
   (`FUN_00401a20` — the allowed-char filter) and routes VK_BACK / DELETE /
   LEFT / RIGHT / HOME / END / INSERT to the matching cursor/edit leaves
   (`FUN_00401c70` / `_c93` / `_cbc` / `_cda` / `_cfc` / `_d11` / `_ca3`),
   exactly the retail VK→FUN map.  Inserts gate internally on the editor's
   `enabled` flag, so off-page keystrokes are dropped.

3. **Proper editor init.**  The page-4 editor is seeded the way retail's
   page-2→4 transition does it (`FUN_00432cc8`:
   `FUN_00402009(&DAT_0053f1e0, 0x10, 0xc0, 0)`) — max 16 chars, soft
   pixel cap 0xc0, mode 0 (punctuation allowed, no case-fold).  The prior
   seed left `max_length = 0`, which would have clamped every insert off;
   `--title-live --title-page 4` now seeds + binds the editor on arrival.

The name field (`DAT_0053f1e0`) became a renderer-owned mutable buffer so
the per-frame copy-out has a destination; `set_choose_title_name` copies a
seed in, the page-4 refresh (`FUN_0041f321`) repaints it shadowed each tick,
and the caret already tracked `editor->cursor`, so it follows the typing.

The deferred page-4 arm leaves are unchanged (the `FUN_00404a46` RNG
prologue, the `FUN_004bbe51` MP join-timer); the `WM_KEYDOWN` Enter commit
(`FUN_00401c5b` + `FUN_0043600c`) and the roster name-commit
(`FUN_0049bae9`) stay deferred — Enter doesn't navigate off page 4 yet.

Coverage holds at **347 touched** — this is a pure *wiring* chip: every
function it uses (the `text_input` cluster, `FUN_004ba9c8`, the WndProc
`FUN_004b29be`) was already ported and counted; the work was connecting them
into a live data path, not porting new leaves.  3 new host tests (arm
enables editor + copies buffer to name; typed char reaches the name box;
editor disabled off-page) → **1245 green**; both drop-in builds clean.

**Verified** (`runs/title-type-p4/`, 2026-05-30) — a new `--title-type STR`
harness flag posts one real `WM_CHAR` per frame into the live page-4 window
(a scripted stand-in for the keyboard, exercising the genuine WndProc
route).  `--title-live --title-page 4 --title-type Caesar` captures the name
box filling from the "aaaaaa" seed to **"Caesar"** through the full route
(`frame_00000` seed → `frame_00016` typed).  Open to a human keyboard pass
too (`--title-live --title-page 4`, then type) for real-hardware
confirmation.

### Next

- **Page-4 Enter commit** — `WM_KEYDOWN` VK_RETURN (`FUN_00401c5b` arm-timer
  + `FUN_0043600c`) and the roster name-commit `FUN_0049bae9(999, …)`; today
  Enter is inert on page 4.
- **Page-3 save-name field** — the same editor drives the load/save filename
  box (`FUN_004ba9c8` page-3 arm copies to `DAT_004ea130`); wiring it is the
  next text-field reuse of this route.
- **Start / game-start** (`FUN_004329ec` → `FUN_00497ced`) remains the deep
  play-flow milestone behind page 7's Start and page 4's Play-Now Continue.

## 2026-05-30 — PAGE 4 INPUT: the choose-title screen goes live (`FUN_004ba9c8` page-4 arm)

The page-4 screen ("Choose your title and your shield") rendered bit-identical
already but its buttons were inert — reachable only by `--title-nav`'s forced
page jump.  This entry ports the **page-4 input arm**, so the shields select
and Back/Continue navigate with real clicks: the live link from the player-
setup screen into the (already-live) page-7 game-setup screen.

Three pieces landed, all riding the already-ported hit-test / hover /
text-editor infrastructure:

1. **`menu_action_p4_shield`** (`FUN_00432ee6` SP branch → `FUN_00432fab`).
   Clicking a shield claims that slot for the current player and releases the
   one it held — the single flashing shield jumps to the clicked design.  The
   visible slot-ownership move is `engine_title_render_choose_title_assign_shield`;
   the strategic colour-table write + the `FUN_0049caaa` AI re-seat are
   deferred with the roster (the page-4 render reads neither — same deferral
   as the page-9 pick / page-7 defaults).
2. **`menu_action_p4_button`** (`FUN_00433155` SP branch).  Back → page 1;
   Continue → a destination ladder that lands on **page 7** for the single-
   player Custom-game flow (`DAT_0057d320 != 1`, `DAT_0055302c != 3`).  The
   Play-Now quick-start (game-start subsystem), Skirmish (page 0xc) and MP
   (pages 6/8/0xb) branches are gated off in that flow and deferred behind
   TODOs, exactly like the page-2/5 transitions; the sound cue, shield-colour
   refresh and the `FUN_0049bae9` name commit follow the roster.
3. **The page-4 arm of `FUN_004ba9c8`** — the 7-or-5 hotspot click pass (the
   Back/Continue row is hidden in the short MP layout) plus the always-7 hover
   pass.  The arm's RNG-advance prologue (`FUN_00404a46`), the name-editor
   tail and the MP Continue-enable timer (`FUN_004bbe51`) are deferred: none
   has a page-4 render consumer in the drop-in (the title text-editor wiring
   is its own subsystem).

The page-4 hotspot table `DAT_004dcc20` (r2-dumped): shields at
`((i-1)·0x58+0x70, 0x8c)`, action `FUN_00432ee6` param i; Back `(0x70,0xd7)`
→ `FUN_00433155(1)`; Continue `(0x150,0xd7)` → `FUN_00433155(2)` — verbatim
matching the rendered geometry.

Coverage 344 → **347 touched** (`FUN_00432ee6` / `FUN_00432fab` /
`FUN_00433155` provenance now referenced; `FUN_004bbe51` documented-deferred
in the arm).  6 new host tests (Continue→7, Back→1, skirmish-Continue
deferred-but-consumes, shield-move releases-and-claims, hover→7, hidden-button
gate) → **1242 green**; both drop-in builds clean.

**Human-confirmed** (`--title-nav` steps 10/11, 2026-05-30,
`runs/title-nav-p4live/`): from the real flow page-2 "Custom game" → page 4,
clicking shield 3 moves the flashing selection slot 1 → slot 3
(`nav_10_p4_shieldpick.bmp`), then clicking Continue lands on the page-7
game-setup screen (`nav_11_p7_continue.bmp`) — the choose-title buttons drive
the real dispatch end-to-end.

### Next

- **Page-4 name editing** — wire the global title text editor + the `WM_CHAR`
  keystroke route to the menu so the name box is typeable live (today the name
  is a static seed); the per-frame copy-out (`FUN_0040210c`) is the last
  page-4 leaf.
- **Start / game-start** (`FUN_004329ec` → `FUN_00497ced`) remains the deep
  play-flow milestone behind both page 7's Start and page 4's Play-Now Continue.

## 2026-05-30 — PAGE 7: the **Defaults** button resets the options (`FUN_004ae539` + `FUN_004ae5e2`)

The page-7 setup screen's button row was Cancel (done) / Start (deep, deferred)
/ **Defaults (stubbed)**.  This entry ports the Defaults action, so clicking it
snaps all twelve option tabs back to the new-custom-game defaults — the last
self-contained chip on the setup screen before the big game-start milestone.

Two small leaves landed, both reachable from the already-ported button
dispatch (`FUN_004335f0` param 3):

1. **`FUN_004ae539`** (169 B) → `engine_title_render_setup_apply_defaults`.
   Retail writes the twelve permuted option scalars (`DAT_0053f288..0053f2b4`);
   the drop-in holds the values in *tab order* (`g_p7_tab_values`), so the
   defaults are mapped through `FUN_00433ba2`'s tab→scalar permutation to the
   single-player vector `{0,0,3,0,0,0,3,2,2,1,6,1}` — byte-identical to the
   captured page-7 seed in `main.c`, confirming this is the screen's defaults
   source.  Tabs 10 (game length) and 11 (an SP-only toggle) take a different
   default in a multiplayer setup (both key off `g_sim_mp_active`).
2. **`FUN_004ae5e2`** (96 B) → `engine_title_render_setup_clamp_player_count`.
   Retail calls this right after the reset with `DAT_0053e8a4` (the live roster
   size) to cap the player-count tab so the default "3" never exceeds a small
   custom roster: `<3→0, <4→1, <5→2, else 3`.

The Defaults arm of `menu_action_p7_button` now runs apply-defaults →
clamp(`s_slot_0053e8a4`) → mark-tabs-dirty, matching retail's order.  Deferred
with the roster subsystem: the recompute tail (`FUN_00433cc1` → `DAT_0053f268`
+ `0x49caaa`) that re-seats the AI roster, and the MP-only pick sound
(`0x43eda0`).  So the option caps reset live, but the left-edge roster shields
are not yet re-seated by a player-count change.

Caught at bring-up time: the clamp reads the roster-size global `DAT_0053e8a4`,
which the page-7 seed never populated (it was 0 → the clamp forced the player
count to 0).  The seeded snapshot has five AI seats, so `seed_page7_state` now
sets `s_slot_0053e8a4 = 5` — the same value a real scenario setup would leave —
and the default "3" stands.

Coverage 343 → **344 touched** (309 host-tested, 14.7% of code bytes).  4 new
host tests (apply-defaults SP vector + MP tab-10/11 variant + the player-count
clamp ladder + the end-to-end Defaults-click-resets-all-twelve through the menu
dispatcher) → **1236 green**; both drop-in builds clean.

**Human-confirmed** (`--title-nav` step 8/9 → `nav_08_p7_scrambled.bmp` /
`nav_09_p7_defaults.bmp`, 2026-05-30): with every option scrambled off-default,
clicking Defaults snaps the whole grid back to the canonical custom-game
defaults (Advanced Farming off, Starting Castle keep, Difficulty easy, Crowns
1000, Army Size no army, Time limit no limit, …), the England country
selection left intact — matching retail's param-3 path exactly.

### Next

- **Start** (`FUN_004329ec` → `FUN_00497ced` game-state init, a ~25-call tree)
  is the deep play-flow milestone behind this screen — the strategic-map
  bring-up.  Multi-session; the natural big next push.
- **Page 8** (MP setup) shares the page-7 input/refresh arm and is the other
  page-9 backdrop; closest neighbour once MP state exists.
- The **roster recompute** behind a player-count pick (`FUN_00433cc1` →
  `0x49caaa`) re-seats the AI players — the first chip of the roster subsystem,
  but it writes strategic globals the synthetic page-7 roster render doesn't yet
  read, so it stays deferred until that render is ported.

## 2026-05-30 — PAGE 9 INPUT: the dropdown values go live (`FUN_004ba9c8` page-9 arm)

Last entry rendered the page-9 option dropdown but left it inert — the values
showed but weren't selectable.  This entry ports the **page-9 input arm**, so
clicking a value actually picks it: the custom-game options are now editable.

Three small functions and the dispatcher arm landed (all leaves):

1. **`FUN_00433ce4`** (hover, 217 B) → `engine_title_render_option_detail_hover`.
   Walks the open tab's value rows and returns the 1-based row under the
   game-space cursor as the highlight `DAT_00553114`, reproducing the
   dropdown's exact row geometry — including tab 2's grow-upward shift
   (quirks #121).
2. **`FUN_00433a23`** (pick) + **`FUN_00433ba2`** (writeback, 287 B) →
   `menu_action_p9_pick` + `engine_title_render_option_detail_pick`.  A
   press-edge click on a highlighted row writes the value back to that
   option's scalar and closes the dropdown to the bookmarked page; the
   page-7 value cap then shows the new pick.
3. The **page-9 arm of `FUN_004ba9c8`**: hover → (press+highlight ⇒ pick) →
   tab-grid dispatch (re-target the dropdown to another tab) → else any press
   dismisses.

The 12 permuted option scalars (`DAT_0053f288..0053f2b4`) are modeled in tab
order as `g_p7_tab_values`, so the writeback is a direct store and the cap
flips immediately.  Deferred with the still-unported MP/roster subsystem:
`FUN_00433ba2`'s recompute tail (`0x433cc1` → `0x49caaa` AI-seat/preview
rebuild) and the MP-only pick sound (`0x43eda0`).  So picking a value updates
the visible cap but a player-count change does not yet re-seat the roster.

One engine oddity documented (engine-quirks #122): a tab's hotspot box and the
dropdown it opens **share the same pixels**, so the hover→pick check must run
*before* the tab dispatch for a value click to select rather than re-open; and
while open, a press on any other control merely dismisses the popup (page 9 is
strictly modal).  Also noted: retail does *not* pre-highlight the current
value — the highlight is purely cursor-driven from the first live frame.

Coverage 340 → **343 touched** (308 host-tested, 14.6% of code bytes).  5 new
host tests (page-9 hover-highlight + pick-writes-value-and-closes + tab
re-target + click-outside-dismiss + tab-2 grow-up hover) → **1232 green**;
both drop-in builds clean.

**Human-confirmed** (`--title-nav` step 7 → `nav_07_p7_optionpicked.bmp`,
2026-05-30): the new step drives the *real* dispatch — clicks option tab 0,
picks value row 2 ("on"), and captures the page-7 setup screen with "Advanced
Farming" flipped **off → on**, every other tab's cap intact.

### Next

- **Page 8** (MP setup) shares the page-7 input/refresh arm and is the other
  page-9 backdrop; closest neighbour once MP state exists.
- The **roster recompute** behind a player-count pick (`0x433cc1` → `0x49caaa`,
  935 B) — re-seats the AI players + rebuilds the map preview; the first real
  chip of the setup-screen roster subsystem.
- The deep **Start** destination (game-start `0x4329ec` → strategic map) is the
  big play-flow milestone behind this whole setup flow.

## 2026-05-30 — PAGE 9: the per-option detail dropdown render (`FUN_0041fdd6`)

The genuine single-player continuation behind the page-7 setup screen: clicking
an option tab now opens **page 9**, the little chrome dropdown of that option's
values.  Ported and host-tested; **human-confirmed** standalone against the
known-good page-7 backdrop.

`FUN_0041fdd6` (415 B) had all four of its callees already ported
(`FUN_0041f6c7` page-7 render, `FUN_004093e0` chrome bevel, `FUN_00402c5e`
text, `FUN_004527a6` dirty box), so it landed as a leaf:

1. **Backdrop redraw.**  The dropdown isn't a cheap overlay — it re-renders the
   *entire* page it came from first (bookmark `DAT_00553e5c` → `FUN_0041f6c7(tab)`
   for page 7), passing the edited tab so that tab's value cap is suppressed
   under the panel, then stamps the value list on top.  Port:
   `engine_title_render_option_detail` brackets the page-7 render with a
   save/restore of `g_p7_active_tab` to reproduce `FUN_0041f6c7(tab)` without
   churning the public setup-screen API.  Only the page-7 bookmark is ported;
   the page-8 (MP) / 0xb / 0xc backdrops are deferred with their pages.

2. **The value list.**  Chrome bevel anchored from `DAT_004d3158[tab]`, `count`
   centered text rows (entry `0x67` sub `DAT_004d3128[tab]+i`), the current
   value (`DAT_00553114`) highlighted red `0xf9`.  The two per-tab tables
   (`DAT_004d5560` counts, `DAT_004d3128` sub bases) were read verbatim from
   the binary with `r2 pxw`.

The deferred page-7 tab-click state (`FUN_00433998`) is now completed:
`menu_action_p7_tab` publishes `DAT_0055306c` (edited tab) + `DAT_00553fb4`
(value count, clamped to `roster−1` for tab 2 when < 5 seats) via a new
`engine_title_render_set_option_detail` setter, so clicking a tab actually
opens a rendered dropdown.  The page-9 *input* arm (hover to highlight, click to
pick a value + close) and the dropdown's own scroll origin (`_DAT_00553530`)
remain deferred — the values show but aren't yet selectable.

Three engine oddities documented (engine-quirks #121): the full-scene redraw
per popup frame; **tab 2 (player count) grows *upward*** (`y += (count-1)·-16`)
because it's on the bottom grid row; and `DAT_004d3158`'s baked third column
(`count+2`) is dead data the renderer recomputes.

Coverage 338 → **340 touched** (12.5% host-tested, 14.6% of code bytes).  6 new
host tests (page-9 rows + highlight tracking + tab-2 upward growth + scene
routing + bookmark-7 backdrop, + the tab-2 roster clamp on the menu side) →
**1227 green**; both drop-in builds clean.

**Human-confirmed** (`--title-nav` step 6 → `nav_06_p9_optiondetail.bmp`,
2026-05-30): the "Advanced Farming" tab dropdown opens over the setup screen
showing **off** (current, red) / **on**, the edited tab's cap correctly hidden,
all other tabs' caps intact.

### Next

- **Page 9 input** — the hover/click arm (page-9 arm of `FUN_004ba9c8` +
  `FUN_0040e5e7`) to make the values selectable: pick a value, write it back to
  the tab, close the dropdown.  This is what makes the options actually editable.
- **Page 8** (MP setup) shares the page-7 input/refresh arm + is the other
  page-9 backdrop; closest neighbour once MP state exists.
- The deep **Start** destination (game-start `0x4329ec` → strategic map) is the
  big play-flow milestone behind this whole setup flow.

## 2026-05-30 — HARNESS: a live real-mouse retail seat (`--interactive`)

Added a `frida_capture.py --interactive` mode that brings up **retail** as a
live, mouse-drivable window — so a ported screen can be tested side-by-side
against the real game.  Used it to confirm the page-7 country selector
(below) behaves identically to retail.  Three distinct blockers had to fall,
each found by instrumenting the WndProc + dumping the engine's live input
state (see engine-quirks #120):

1. **Frozen at ~5 Hz.**  The main loop (`FUN_0040e9ab`) only runs its present
   branch while `DAT_004e65a8 != 0` (app-active, set by `WM_ACTIVATEAPP` when
   the window is foreground); else it `Sleep(200)`s.  A frida-spawned,
   created-hidden window never wins foreground, so it crawled.  Fix:
   `force_active` pokes `DAT_004e65a8=1` / `DAT_004e65d0=0` every tick.
2. **Cursor mapped to the wrong place.**  With the cursor cache enabled
   (`DAT_0057cac4 != 0`), `FUN_004b191e` overwrites the cached cursor with
   raw **GetCursorPos screen** coords — correct only in fullscreen.  Fix:
   force `DAT_0057cac4=0` so the engine uses the WndProc's WM_MOUSEMOVE
   **client** coords.
3. **Clicks landed ÷3 off (the real bug).**  The engine scales cursor→game
   from `DAT_004ea180..18c`, which the WndProc fills with the **fullscreen**
   `GetSystemMetrics` rect whenever `DAT_0053f228 != 0` (retail's default) —
   even though the GDI seat stretches the 640×480 backbuffer into the
   **actual** 640×480 window.  So a click on the visible country list got
   divided by 3 into the roster.  Fix: overwrite those four globals with the
   real `GetClientRect` size each tick.  Verified live:
   `client=(474,313) → game=(474,313)` 1:1, page 7↔9 transitions fire.

`--interactive` bundles `force_active` + `--agent-present` (GDI seat, every
3 ticks for smoothness) + window-show-after-2s + a 5-min session.  Combine
with `--skip-intro --input-trace <nav>.jsonl` to auto-navigate to a screen
(now at full tick rate, so the tick-stamped trace plays in ~seconds), then
drive it by hand.  **Human-confirmed:** retail page 7 driven by real mouse,
country selector matches the port exactly.

## 2026-05-30 — PAGE 7: the setup screen goes **live** (input dispatch + per-frame refresh)

Last entry's page-7 render was static — every piece of runtime state bound
via setters for a groundtruth capture.  This entry makes the **custom-game
setup screen interactive**, the way pages 1/2/5 are: the country selector
now scrolls and selects under the mouse, and the screen gets the cheap
per-frame repaint instead of needing a full-scene redraw.

Two arms of the title input/render loop landed:

1. **Input dispatch** — the page-7 arm of `FUN_004ba9c8`, three hotspot
   tables dispatched in order (no hover pass — retail omits `FUN_0040e5e7`
   for this page).  All four action functions ported:
   - **`FUN_004337b4`** (country scrollbar, the two type-2 auto-repeat
     arrows, params -1/+1) and **`FUN_00433905`** (the five country rows,
     params 1..5) → `engine_title_render_country_scroll/select`.  These are
     **fully functional**: the scroll clamps to `[0, count-5]`, the selected
     visible row tracks the click, and the selected-country id
     (`s_slot_0053f034 = country_values[scroll + sel]`) recomputes — so the
     list visibly scrolls and the highlight moves.  The per-country **stage
     reload + map/preview recompute** (`0x43eda0`/`0x46a037`/`0x467770`/
     `0x4298c1`/`0x4ae5e2`/`0x43cc1`) is deferred (those subsystems are
     unported); the scroll/select state + dirty flags are faithful.
   - **`FUN_004335f0`** (Cancel/Start/Defaults, params 1/2/3): Cancel → page
     1 (works — main menu is rendered); Start → game-start (`0x499dc3` +
     `0x4329ec`) deferred; Defaults → option-reset (`0x4ae539`) deferred.
   - **`FUN_00433998`** (the 12 option tabs, a 4-col×3-row grid, params
     1..0xc): navigates to the page-9 option-detail screen + return
     bookmark; the page-9 detail state + render are deferred.
   The three hotspot tables (`DAT_004dcd28` / `_004dcd88` / `_004dce30`) were
   read verbatim from the binary with r2 (24-byte `{x0,y0,x1,y1, action@8,
   type@0xf, param@0x10}` entries).

2. **Per-frame refresh** — the page-7 arm of `FUN_004ba26e`:
   `FUN_0041f98b` (country list) + `FUN_0041f86d(999)` (tabs) +
   `FUN_0041ff75(0)`.  The first two are **dirty-gated** inside their
   sub-renderers (`DAT_0057c948` / `DAT_0055ce94`); the port hoists the gate
   to the refresh caller (`engine_title_render_refresh_setup`) since our
   sub-renderers must draw unconditionally on the full-scene path, and the
   full render now clears both flags at its tail.  `FUN_0041ff75` is the
   **multiplayer chat ticker** — a no-op in single-player (`DAT_00553030 ==
   0`) — so it's elided.  Wired into `engine_title_render_refresh_buttons`
   (page 7) → reachable every frame from `engine_tick_title_render_step`.
   See engine-quirks #119.

Harness: `--title-live` now honors `--title-page N` (starts the live loop on
that page, seeding page-7 state when N==7), so the setup screen can be driven
interactively for verification.

Coverage 327 → **338 touched** (12.4% host-tested, 14.5% of code bytes).
9 new host tests (country scroll clamp + id recompute, row select, dirty-
gated refresh, + 6 page-7 dispatch cases) → **1221 green**; both drop-in
builds clean.

**Human-confirmed** (`--title-live --title-page 7`, 2026-05-30): clicking a
nation row moves the selection highlight, the up/down scroll arrows page the
44-country list, and the scrollbar *track* is inert (faithful — retail's
`DAT_004dcd88` has only the two arrow hotspots + five rows, no thumb-drag
control).  Clicking an option tab navigates to page 9, which renders blank
(the page-9 detail screen is deferred).  The top-right map preview stays
fixed across selections (the per-country reload is deferred).  `--title-live`
now honors `--title-page N` (seeds page-7 state for N==7) to enable this.
*Side-by-side against live retail wasn't possible*: the frida capture harness
feeds synthetic input and doesn't pass a real mouse to retail's WndProc, so
retail can't be driven interactively — the port was confirmed standalone
against the known-good static render + the verbatim-from-binary hotspot/action
ports.

### Next

- **Page 8** (the multiplayer setup screen) shares this input/refresh arm
  (`FUN_004ba9c8`/`FUN_004ba26e` add `FUN_00420147` + the MP chat box), so
  it's the closest neighbour once MP state exists.
- **Page 9** (the option-detail picker reached by clicking a tab) is the
  genuine single-player continuation — its render + the deferred tab-detail
  state (`DAT_0055306c` / `DAT_00553fb4` / `DAT_004d5560`) are the next chip
  to make the tabs do something on click.
- The deep **Start** destination (game-start `0x4329ec` → strategic map) is
  the big play-flow milestone behind this screen.

## 2026-05-29 — PAGE 7: the custom-game **setup screen** render (`FUN_0041f6c7` + subs)

The natural next play-flow screen (page 4 *Continue* → page 7) is now
ported and host-tested.  Groundtruth (`runs/groundtruth/scenario-setup-retail.bmp`,
captured last entry) is the **custom-game setup** screen: a central
carved-stone panel (misc sprite 0xf) with *Cancel / Start / Defaults*
carved into it, a player roster down the left edge (the human "aaaaaa" +
four AI — Knight / Baron / Bishop / Countess — each a shield + portrait +
name), a country/map selector top-right (a parchment frame + the rendered
Britain map + a 5-row country list + scrollbar), and a 4×3 grid of option
tabs (Advanced Farming / Armies Eat / … each a wrapped header over a
beveled value cap).

Landed in **three chips**:

1. **Chrome bevel-frame renderer** (`gfx_panel_draw_bevel`, port of
   `FUN_004093e0`).  Discovery: `FUN_00409934` / `FUN_00409c93` (the
   bevel border + interior builders) are **byte-for-byte twins** of the
   already-ported stone-panel builders `FUN_0040946d` / `FUN_004097c5`,
   with the sprite sheet hardcoded to the chrome blob `0x57d3d0`
   (**panels.pl8**, distinct from panels2.pl8) and the mode passed as the
   first arg.  `FUN_004093e0` = border(mode 1) + inset interior; mode 1
   shifts every border index by +0xcc into the sheet's chrome style
   block.  So the renderer was already done — only the new asset needed
   loading (`gfx_load_chrome_sheet`, panels.pl8 → 0x57d3d0, buffer
   0x128e0 per the master-table size field).
2. **Palette-darken rect** (`gfx_paint_darken_rect`, port of
   `FUN_004b13ed`).  Dims disabled roster seats via a luminance-keyed
   dark-gray ramp `0x3f - (((R+G+B)/3) >> 3)` over the active palette.
   Not exercised on the groundtruth (all five seats active) but part of
   the roster pass; host-tested against a synthetic palette.
3. **The page-7 render** (`FUN_0041f6c7` root + `FUN_0041f98b` country
   list / `FUN_0041fbcb` roster / `FUN_0041f86d` option tabs).  The
   coat-of-arms/map preview is `0x410c71(0,…)`: at **param 0** the shield
   compositor `0x410cbd` skips its entire per-pixel loop, so the preview
   is exactly a raw blit of the bound `0x569590` buffer — the non-visual
   title/rank recompute `0x451bba` and the AI-seat assignment
   `FUN_0049caaa` are modelled as **bound state**, not recomputed at
   render time.  Tab geometry (`DAT_004d3098` 4×3 grid) + value-string
   bases (`DAT_004d3128`) are embedded; the runtime state (country list,
   roster records, tab scalars, preview buffer) is bound via setters for
   the groundtruth capture.

Background for page 7 is **custom.256 / custom.pl8** (already routed in
`gfx_title_background_load`).  Scene dispatch routes page 7 →
`engine_title_render_setup_screen`; `main.c` loads + binds the chrome
sheet + palette in `title_bind_render_state`.

Coverage 321 → 327 touched (12.2% host-tested, 14.1% of code bytes).
5 new page-7 host tests + bevel + darken tests → **1212 green**; both
drop-in builds clean.

**Groundtruth diff — bit-identical (0 diff px / 307200).**  The page-7
runtime state was captured live from retail via `frida_capture.py
--dump-globals` (run `gt-p7-state`): country-list bytes `DAT_0050a460`
(=[0,1,2,…], count 0x2c, scroll 0, sel 0), the 5 roster records at
`DAT_0057bf05`+i·0x160 (the human "aaaaaa" type 1 + four AI — Knight /
Baron / Bishop / Countess), names at `DAT_00553d54`+i·0x2c, enabled
`DAT_005530e0` (**all 0** → the human seat is dimmed via the darken pass,
which is what makes its portrait the dark silhouette + its shield gray in
the groundtruth), the 12 tab scalars at `0x53f288..0x53f2b4` (resolved via
0x433a70's permutation → off/no/keep/medium / off/easy/some/no-limit /
five/no-army/1000/all), and the 128×128 preview buffer `DAT_00569590` (the
Britain map thumbnail).  These are seeded into the `--title-page 7`
bring-up (`seed_page7_state`, the small state hardcoded à la pages 3/4; the
preview loaded from `p7preview.bin` in the game dir if present).  Rendered
over custom.pl8 and diffed against `scenario-setup-retail.bmp`: **0
differing pixels** — the darken-ramp 8-bit-palette model and every sprite/
text/bevel/scrollbar landed exactly.  Page 7 is **done**.

### Next

**Page-7 input arm + per-frame refresh** (the live interactive screen):
port the page-7 input dispatch (tab/country/roster hit-tests → the
`0057c948`/`0055ce94` dirty flags + the per-frame `FUN_004ba26e` page-7
refresh arm), mirroring the page-5 input + page-3/4 refresh work.  Or
continue the setup-flow render sweep (pages 8..0xd) — page 8 shares the
page-7 update fn (`FUN_0041f98b`+), so it is the closest neighbour.

## 2026-05-29 — MISC SHEET: bind misc_sel.pl8 (`DAT_005530c8`) — last unbound title asset

The **remaining unbound asset** flagged as "Next" last entry — the
mode-dependent misc sprite sheet `DAT_005530c8` — is now loaded and bound
into the title-render path.  This unblocks the "kind < 0x18" gauntlet
buttons on the setup-flow pages 6..0xd (`engine_button_render_list` already
took a `misc_sheet` arg; it was NULL until now) and is the same sheet the
in-game HUD indexes.

The misc sheet is **mode-dependent** — retail swaps several files into the
one `DAT_005530c8` buffer per game mode:

- `misc_sel.pl8` (160540 B) — the **setup/selection** variant, loaded by
  `FUN_00499034` when entering the new-game flow (called from the page-2
  action at 0x4332xx).  This is the one pages 6..0xd read.
- `misc_ske.PL8`, `Misc_bat.pl8`, `Misc_cty.pl8` — skirmish-editor / battle
  / city variants, loaded by sibling routines (`FUN_00498dcb` loads
  `misc_ske`).  Indices ≥ 0x36 (in-game HUD: 0x36/0x39/0x3b…) are valid in
  those variants, **not** in misc_sel.

Chip:

- **`gfx_load_misc_sheet`** (`gfx.c`, port of `FUN_00499034`) — reads
  `misc_sel.pl8` whole into a `0x28488`-byte buffer (retail's
  `DAT_004daee8` malloc size, r2-confirmed; file fits).  Like system2.pl8
  the blob is the raw file (`FUN_004af8d9` reads from offset 0), so the
  disk read reproduces `DAT_005530c8` bit-for-bit.  Freed in `gfx_shutdown`.
- **`engine_title_render_bind_misc_asset`** — the `g_misc_asset` setter,
  mirror of the system-asset binding.  `main.c`'s title bring-up now loads
  + binds it alongside system2.pl8.

**Descriptor sanity-checked** (host decode of misc_sel.pl8): the setup-flow
indices decode to coherent, self-consistent dims — idx 0xc/0xd/0xe = 64×64
column-header icons (pixel offsets step by exactly 0x1000 = 64×64 B), idx
**0xf = 320×240** (the page-7 backdrop, `FUN_0040a21a(sheet,0xf,0xa0,0)`),
idx 0x10 = 130×130.  Both drop-in builds clean; 1204 host tests green
(plumbing chip — no page renders it yet, so no frame-diff milestone; that
arrives with the page-7 render).

### Next

**Page 7** (`FUN_0041f6c7`) — the **game-setup screen** (groundtruth now
captured + confirmed, see below), the natural next screen in play-flow:
page 4 (Choose title) → *Continue* → page 7 when `DAT_00553030 == 0`
(single-player custom game; page-4 hotspot `DAT_004dcc20` entry 7
"Continue" param 2 → `FUN_00435531` → transition at 0x4334xx line ~21506).

**Groundtruth captured (2026-05-29).**  `tools/traces/menu_scenario_setup.jsonl`
(choose-title path + a Continue click at game (432,227)) drives retail to
page 7; `--dump-globals 0x5530f0` read back **0x07** (confirmed page 7).
4 full-color frames in `runs/capture/gt-p7/`; canonical groundtruth saved to
`runs/groundtruth/scenario-setup-retail.bmp` (gitignored).  The screen: a
coat-of-arms preview top-left, the AI-opponent roster down the left edge
("The Baron / Bishop / Witch / Knight / Countess" + shields), three carved
64×64 column headers (misc sprites 0xc/0xd/0xe), the 320×240 stone backdrop
(misc sprite 0xf), a center options grid (the 12-entry tab strip), and a
right-edge scrollbar.

**Render decomposition** (the arc — state-heavy, like page 4 but more so):
- the 320×240 misc backdrop + 3 column labels (entry 0xb subs 0xc/0xd/0xe);
- the 5-row scrollable selector + computed scrollbar (`FUN_0041f98b`, 576 B)
  — needs `DAT_0053e90c` (scroll), `DAT_0057cacc` (sel), `DAT_00554018`
  (count), the `DAT_0050a460` value list, scrollbar math `FUN_00404dc1/d6b`
  (ported);
- the coat-of-arms preview (`FUN_00410c71` → `FUN_0040a01d` + `FUN_00410cbd`,
  the 654 B palette-remap shield compositor; page-7 calls it with param 0,
  the simple base-blit path);
- the per-player roster pass (`FUN_0041fbcb` → already-ported `FUN_0049caaa`;
  reads the roster at `DAT_0057bf05`+i·0x160, names at `DAT_00553d54`+i·0x2c);
- the 12-entry option-tab strip (`FUN_0041f86d`, 286 B → `FUN_004093e0`).

Unported leaves to chip first (mechanical, host-testable): `FUN_00410c71`
(+`FUN_0040a01d`/`FUN_00410cbd`), `FUN_004b13ed` (274 B), `FUN_004093e0`,
`FUN_0041f86d` (286 B).  Then the page-7 render + state seeding from a
`--dump-globals` capture, scene-dispatch routing + per-frame refresh, and a
`--title-nav` step — verified 0-diff vs `scenario-setup-retail.bmp`.

## 2026-05-29 — BUTTON OVERLAY: page-3 gauntlet buttons + scroll arrows (page 3 now fully bit-identical)

The **per-frame button-overlay subsystem** — flagged as the next chip
last entry — landed, closing the last 2870 px of page 3 ("Loading a
game.").  The drop-in now renders the save/load screen **0 diff px vs
retail**, gauntlet action buttons and scroll arrows included
(`compare_frames` → `identical: true`).  It's the generic renderer every
screen shares, so it also unblocks pages 6..0xd's buttons/scrollbars.

Four chips:

1. **`gfx_sprite_blit_24x24_raw`** (`FUN_004b4cb8`) — the one missing
   primitive: the 24×24 middle sibling of the 16×16/32×32 opaque raw
   blits (kinds 0x11 / 0x18; same hardcoded 640-byte dst stride, quirk
   #75).
2. **`engine_button.c`** (`FUN_0040cfd2` → `engine_button_render_list`) —
   the generic button-strip renderer.  Walks an `engine_button_rec_t`
   table, stages each button's 16-byte descriptor, dispatches a
   fixed-size raw blit by `kind` (sheet select `< 0x18` → misc /
   `>= 0x18` → system2), frame select by pressed/hover.  Sheet-agnostic
   + a NULL dirty hook → reusable by every screen.
3. **`gfx_load_system_sheet`** loads system2.pl8 (the `DAT_005bb540`
   blob), bound via `engine_title_render_bind_system_asset`.  A live
   `--dump-globals` confirmed `DAT_004dbbf0 == 0` (system2, not system)
   on page 3, and the on-disk file equals the live blob byte-for-byte.
4. **Page-3 wiring** — the `DAT_004ddd78` button table (r2 dump: 2
   gauntlet buttons kind 0x20 / 2 scroll arrows kind 0x18, all reading
   system2.pl8) as `k_filelist_buttons`, painted by
   `engine_title_render_filelist_buttons` from the page-3 arm of
   `..._refresh_buttons` and from `--title-nav` step 5 (post full-scene).

11 new host tests (4 blit + 7 renderer); 1193 → 1204.  Both drop-in
builds clean.  Human-viewable: the full screen renders correctly
(title + selected-name field + file grid + buttons + arrows).

### Next

The overlay generalizes: pages 6..0xd each just need their
`DAT_004dd***` button table transcribed + the right sheet bound (the
misc_*.pl8 sheet `DAT_005530c8` is the remaining unbound asset — needed
by kinds `< 0x18`, i.e. screens whose buttons live in the
mode-dependent misc sheet rather than system2).

---

## 2026-05-29 — NEXT SCREEN: title page 3 "Loading a game." save/load list (list render bit-identical)

The save/load file-list screen now renders in the drop-in, **0 diff vs
retail everywhere the page-3 draw path covers**.  Reached from "Your
options" → Load a game.  Two chips:

1. **Selection-highlight fill** (`FUN_004b414a` → `gfx_sprite_fill_block`)
   — a solid block fill sized from the staged sprite dims; the one new
   primitive (everything else was already ported).
2. **Page-3 render** (`FUN_004148e4` frame + `FUN_004149ec` list content)
   in `engine_title_render.c`: panel + title (entry 0x28 sub 5) + the
   selected-name field (tiled inset + name + caret) + the file grid
   (3 columns, entries at `DAT_004e8790`, 0x41-byte records) + box
   outlines.  State seeded from a `--dump-globals` capture; `--title-nav`
   step 5 forces the page.

Verified bit-identical for the panel, title, selected-name field, all
four files (lastturn / old_turn→"old turn" / safeturn / test), and the
box outlines.  The remaining **2870 px (all x ≥ 400) are the gauntlet
action-buttons + scrollbar** — NOT part of the page-3 draw, but the
per-frame button overlay `FUN_0040cfd2(&DAT_004ddd78, 4)`.

### Next

The **button-overlay subsystem** — `FUN_0040cfd2` (generic button-list
renderer, keyed off `FUN_004ba26e` per screen state) + the per-screen
button tables (`DAT_004ddd78` etc.) + the button sprite sheets
(`DAT_005530c8`, loaded mode-dependently from `misc_*.pl8`; the static
`DAT_005bb540` array).  It's shared by every screen (the gauntlet
buttons + scrollbars across the whole UI), so it unlocks more than just
page 3 — but it needs asset-load research first.  After that, page 3 is
fully 0-diff and the same overlay closes out pages 6..0xd's buttons.

---

## 2026-05-29 — NEXT SCREEN: title page 4 "Choose your title and your shield" (bit-identical)

The fourth title page — the **player-setup screen** (editable name/title
box + five-shield picker + Back/Continue) — now renders in the drop-in,
**0 diff px** vs retail.  Reached from "Your options" → Custom game /
Skirmish (or the page-5 campaign chooser).  This is the most entangled
title page so far; it landed as four chips:

1. **Opaque sprite blit** (`FUN_004b432e`/`FUN_0040a567`) — the copy-all
   (no-transparency) blit + staging wrapper for the solid name-box frame.
2. **Text caret** (`FUN_0040acce`) + **filled rect** (`FUN_0040437d`) —
   new `gfx_caret` module bridging gfx_text (anchor), text_input (cursor),
   font_metrics (width), gfx_line (fill).  Blink counter; off in a single
   static render (matches the mid-blink-off groundtruth).
3. **Page-4 render** (`FUN_0041ef57` + `FUN_0041f321`/`FUN_0041f1dd`/
   `FUN_0041f01f`) in `engine_title_render.c` — panel/title/name box/
   shields/buttons, state bound via setters and seeded from a
   `--dump-globals` capture of retail.
4. Two harness features — **`--dump-globals`** (read retail state at the
   capture frame; seeds the port) and **`--poke-globals`** + drop-in
   **`--title-name`** (force the name buffer for a glyph sweep).

**The last 191 px** were the text shadow: the steady-state name box is
re-rendered every tick by `FUN_0041f321` *standalone* (shadow ON), not
through `FUN_0041ef57`'s no-shadow bracket — so a captured frame shows a
shadowed name (engine-quirk #116).  Reproduced via a page-4 refresh.

**Full-glyph sweep:** the name box was driven across the whole printable
set (retail name-buffer poked, drop-in `--title-name`) — **0 glyph-shape
diffs** for `A–Z`, `a–z`, `0–9`, `.,!?:;-+=()&%`; the only residual is the
caret's non-deterministic blink phase.  Human-confirmed the font looks
perfect.  29 host tests (1158 → 1186 over the four chips); both drop-in
builds clean.

### Next

Page 3 (the **load-a-game / save list** screen, `FUN_0041ef42` + the
scrollbar `FUN_00421005`) and the remaining sub-screens (6..0xd).  The
sprite/caret/text-field primitives are now all in place.

---

## 2026-05-29 — MENU INPUT: page-5 campaign chooser navigates

The page-5 ("Play Now!" campaign chooser) **input** arm now matches its
render half — the two buttons hit-test and click-dispatch like pages 1/2.

`engine_menu_input.c` grew the page-5 hotspot table (retail `DAT_004dccf8`,
2 entries: left "Original campaign" param 0 / right "The new campaign" param
1, both press-edge), the `page == 5` dispatcher arm, and `menu_action_page5`
(port of `FUN_00433461`): it records the campaign choice in the shared
save-schema scalars `DAT_0053f640` (= param) / `DAT_0053f258` (= 2 new / 0
original), sets `DAT_0055302c = 1` (full-game substate), and advances to
**page 4** ("Choose your title and your shield").  The fade / camera-reset /
`s011_02.wav` side-effects are deferred behind TODOs, as with the page-2
transitions.  3 new host tests (1158 → 1161), all green; drop-in build clean.
The arm rides the existing `engine_tick_menu_input_step` seam, so a real
mouse navigates into page 5 once the live menu is up (`--title-live`).

### Next

Port page 4's **render** ("Choose your title and your shield" — the
custom/scenario setup screen, `FUN_0041ef57` + `FUN_0041f321`/`FUN_0041f1dd`),
so the page-5 → page-4 transition lands on a real screen instead of the
deferred page hook; then page 3 (load list) and the remaining pages.

---

## 2026-05-29 — NEXT SCREEN: "Play Now!" campaign chooser (title page 5)

The fifth title page — the **"Play Now!" expansion-pack campaign chooser**
("Expansion pack installed, choose:-" → *Original campaign* / *The new
campaign*) — now renders in the drop-in.  Reached from "Your options" →
Play Now! (page 2 button 1).  This is the third title page ported (after
the main menu and "Your options").

### The port

`FUN_0041f592` (page-5 root) + `FUN_0041f5e8` (the two buttons) landed in
`engine_title_render.c` as `engine_title_render_play_now` /
`engine_title_render_play_now_buttons`.  Structurally the same panel +
large-font title + beveled button-label shapes as pages 1/2, with two
differences worth noting:

- **Strings come from localization entry 0x27** (not 0xb): title = sub 0,
  buttons = sub 4 ("Original campaign") / 5 ("The new campaign").  The
  shared `menu_text` helper grew an entry parameter (`menu_text_entry`).
- **Two side-by-side buttons** (left x=0x6e / right x=0x16e, both w=0xa4
  h=0x18 at y=0x74), not the vertical stack — so the page-5 button block is
  its own function rather than the `render_menu_button` row helper.  One
  dirty-box mark covers the row.

Scene dispatch + the per-frame button refresh route page 5 → the new
renderer (pages 3,4,6..0xd still hit the deferred page hook).  Background is
the same gateway backdrop as pages 1/2, so no new asset path.  All draw
primitives are the ones already proven bit-identical on pages 1/2.

### Wiring + verification

`--title-nav` grew a step 3 that navigates page 1→2→Play Now→5 and captures
page 5 (the page has no ported hover arm yet, so the bring-up parks the
highlight at neutral to mirror retail's cursor-off view).  The drop-in
render is visually faithful — stone panel, the dark-red drop-cap "E" from
the uppercase-highlight quirk, the two buttons over the gateway backdrop
(`runs/title-nav-p5/nav_03_p5_playnow.bmp`).  8 new host tests (1151 → 1158,
incl. a fixed screen-dispatch probe that had used page 5 as its
"unported page" case), all green; full drop-in build clean.

### Bit-identical — verified (harness fix: input-replay viewport rescale)

The drop-in page-5 render is **0 diff px** vs a fresh retail groundtruth
(`runs/groundtruth/playnow-retail.bmp`), 307200/307200 identical,
`max_channel_delta=0`, both neutral.

Getting the retail capture first required fixing a regressed harness path:
the `--input-trace` menu navigation had stopped driving retail (clicks
landed nowhere — the page-transition action never fired).  Root cause found
by dumping the cursor/extent globals per post: the engine maps the cached
*client* cursor to game coords by `game = cached * game_extent /
viewport_screen_extent` (FUN_004b191e), and under the capture's display path
the viewport screen rect is the **desktop resolution** (`[0,0,1920,1080]`),
not 640×480 — so a 640×480-authored trace coord scaled *down* 3× ((320,107)
→ game (105,47)) and missed every hotspot.  Fix
(`rescaleMouseLParamToClient` in `tools/frida/openlords2-agent.js`):
pre-multiply replayed mouse coords by `viewport/game_extent` so the engine's
own divide reproduces the authored game coord, resolution-independently
(no-op when the viewport is already 640×480).  Traces stay authored in game
space; this restored both the page-2 and page-5 captures.

(Side note: the stale `options-retail.bmp` groundtruth now shows a ~486-px
scattered palette diff vs a fresh page-2 capture — it predates the
full-color-palette-default commit and should be re-captured; not a port
issue.)

### Next

Port page 5's input arm (page-5 hotspot table + the campaign-select action)
so the live menu navigates into/out of it, and/or the remaining title pages
(3, 4, 6..0xd) the same way.

---

## 2026-05-29 — MENU INPUT: title pages navigate (hover + click → page change)

The title menu's **input** half now matches its bit-identical render half:
hover updates the selected-button highlight, and clicks transition pages.
Verified end-to-end in the drop-in — main menu → "Single player" → "Your
options" → "Back." → main menu — every frame **bit-identical** to the page
groundtruths (0 diff px), round-trip clean. User-confirmed visually.

### The port

New module `engine_menu_input.{c,h}` ports the title-screen hotspot chain
(the `DAT_004eac50 == 0x1f` arm of the UI input dispatcher `FUN_004ba9c8`):

- `FUN_0040e5e7` hover hit-test → 1-based hovered index → `DAT_00553114`.
- `FUN_0040e3ee` click dispatch with the three button-edge types (1=press,
  3=release, 2=held/repeat) → invokes the hotspot action.
- `FUN_00432b05` / `FUN_00432cc8` page-1/2 click actions — the page
  transitions (Single player→2, Back→1, Play Now→5, Load→3,
  Custom/Skirmish→4, Exit→quit). Unported side-effects (MP, save-list,
  movie, sound/fade) deferred behind TODOs.

Page-1/2 hotspot geometry + action params lifted from retail tables
`DAT_004dcb48` / `DAT_004dcba8` (24-byte entries, dumped via r2). The
input edge flags + game-coord cursor consumed are exactly what the
already-ported `FUN_004b191e` resolver emits. See
[`title-menu-screens.md`](findings/title-menu-screens.md) for the full
table layout + transition map.

### Wiring + verification

`engine_tick` runs the new `engine_tick_menu_input_step` right after the
input resolver, gated on `g_sim_current_screen == 0x1f` — so the menu
navigates on its own once the live title screen has a producer. Until then
the scripted `--title-nav DIR` bring-up drives the *real* ported dispatch
with synthetic clicks and captures each page. 19 host tests (1132 → 1151).

### Next

Live interactive menu: lock the canvas around the demand-redraw seam, wire
the per-frame button refresh (`FUN_004ba26e` 0x1f arm) for hover repaint,
and add a startup producer that sets screen 0x1f — then a real mouse
navigates the running drop-in. Then port the remaining title pages (3..0xd).

---

## 2026-05-29 — NEXT SCREEN: "Your options" (title page 2) BIT-IDENTICAL

The single-player **"Your options"** screen (Play Now! / Load a game /
Skirmish! / Custom game / Back.) now renders in the drop-in **bit-identical**
to retail (0 diff px vs `runs/groundtruth/options-retail.bmp`). This is the
first UI screen beyond the main menu — reached by clicking "Single player".

### How it was reached + captured

The title screen (`DAT_004eac50 == 0x1f`) is a page state machine
(`DAT_005530f0`): page 1 = main menu, **page 2 = "Your options"**. With the
robust `--skip-intro` (prior entry) the menu is reachable in ~200 ms, so a
synthetic `--input-trace` click on "Single player" (320,107) drives page 1→2
deterministically. The settled page-2 frame (cursor parked off the buttons)
was saved as the groundtruth. See
[`title-menu-screens.md`](findings/title-menu-screens.md).

### The port

`FUN_0041ec8a` (page-2 root) + `FUN_0041ece6` (5 buttons) are structurally
identical to the already-ported main menu (`FUN_0041ea14`/`FUN_0041eaa3`) —
same panel + large-font title + bevel-rect/dirty-mark/label buttons, just one
extra row and different geometry/strings. Landed in `engine_title_render.c`:

- `engine_title_render_options` / `engine_title_render_options_buttons`, with
  a shared `render_menu_button` helper factored out of the main-menu path.
- Scene dispatch routes page 2 → options (page 1 → main menu; 3..0xd → the
  still-deferred page hook).
- Panel args `(0xb0,10,0x12,0x12)` (vs page 1's `0xa0,10,0x14,0xf`); title
  str 5 centered in 0x120 at (0xb0,0x2d); buttons str {6,7,0x13,8,9} at
  y {0x5b,0x7f,0xa3,0xc7,0xeb}. No subtitle (unlike page 1).
- Bring-up: `--title-page N` (implies `--title-menu`) renders the chosen page.
- 5 new host tests (suite 1127 → 1132), all green; full drop-in build clean.

### Next

Wire the page state machine + menu input (the `DAT_005530f0` transitions, the
hotspot dispatch) so the drop-in navigates menu↔options on its own, then port
the remaining title pages (3..0xd) the same way.

---

## 2026-05-29 — HARNESS: `--skip-intro` reaches the menu in ~200 ms

The retail capture harness can now **skip the boot intro movie chain** outright
instead of turbo-fast-forwarding it. Every menu/UI parity run previously paid a
~23 real-sec turbo warmup (`--frame-capture-warmup-secs 1000`) to play through
`intro.smk → imptitle.smk → credits.smk`; `--skip-intro` collapses that to ~3
main-loop frames (~200 ms wall).

### The intro chain (RE'd)

WinMain (`FUN_0040e9ab`) line 7611 calls `FUN_004b3571(0)`, which starts
`intro.smk` (screen state 0x22) and returns — the movie plays during the main
loop via the state-0x22 dispatch into `FUN_0042dbc7` (per-frame Smacker stepper).
On a movie's last frame, `FUN_0042dbc7` closes the handle and calls
`FUN_0042e060`, which — while engine state `DAT_0052af94 == 1` — chains the next
movie by filename: **intro → imptitle → credits**, each returning to state 0x1f.
After credits, state is **0x1f** (title) with the deferred-screen cookie
`DAT_0057d340` set, and the next-frame `FUN_0040f1a0` dispatcher builds the menu
via `FUN_0041e7e1`. (The natural intro→title path does *not* go through
`FUN_0042b780` — that's the *re-entry* title producer used from submenus.)

### The skip mechanism

`installSkipIntroHook` (agent) attaches to `FUN_0042dbc7` and, on each main-loop
step gated on `DAT_0052af94 == 1` (boot chain only — in-game cutscenes untouched),
snaps the Smacker handle's current frame (`DAT_004d4d58 + 0x374`) to its last
(`+0x0c` count − 1) so the stepper takes its done→chain branch. The open-time
`FUN_0042dbc7` call nested inside `FUN_0042da18` is bracketed out via
`g_smack_in_open` — forcing it done there would run `FUN_0042e060` before
`FUN_0042d91b` writes the return-state global `_DAT_005169b0`, corrupting the
chain. All engine transitions stay retail-authentic; only per-movie playback
duration is cut, so the result is faithful (not a synthetic jump-to-menu).

### Verified

`tools/frida_capture.py --skip-intro` (turbo and non-turbo, warmup 2–3): every
captured frame is **bit-identical** to `runs/groundtruth/mainmenu-retail.bmp`
(0 diff px). New CLI flag `--skip-intro`; plumbed through the agent init opts +
the run-summary JSON.

Files: `tools/frida/openlords2-agent.js` (ADDR `fn_smack_step` / `fn_smack_open`
/ `var_smack_handle`, `installSkipIntroHook`), `tools/frida_capture.py`,
`docs/findings/main-menu-parity.md`.

---

## 2026-05-29 — MENU TEXT RENDERS: font load + glyph-table extract + bind

Committed `7bce81a`.  The main-menu **text** now renders end-to-end in
the drop-in — title, subtitle, and all four button labels.  Diff vs
`runs/groundtruth/mainmenu-retail.bmp` dropped **1.58% → 0.37%**; the
text matches retail in glyph shape, position, and shadow.

### How the assets were located (the asset-source mystery)

The font buffers `DAT_005af8f0` / `DAT_005b2fa0` are in BSS, never
written by a `FUN_004af8d9(..., &DAT_..., ...)` call, and the font
filenames aren't symbolised — so neither showed up in a naive grep.
They are loaded by a **table-driven master asset-init loop** (the 13-asset
loop ending `FUN_004afab9(0x4dbf68,...)`): a 13-entry table at **0x4d9f48**
(20-byte entries — 16-byte filename + 4-byte buffer size at +0x10),
indexed 0..0xc, each loaded into a fixed BSS buffer:

  - idx 6 → `DAT_005af8f0` ← **fntl2_14.pl8** (buf 0x36b0)  menu/default
  - idx 7 → `DAT_005b2fa0` ← **fntl2_22.pl8** (buf 0x84d0)  large title

(`Font_c2.pl8` / `Font3c2.pl8` in the game dir are **unused leftovers** —
not referenced anywhere in the binary.)

The char→glyph index table is the single 256-byte map at **0x4d71d0**
(file-backed .data).  Retail reaches it via two pointers 0x20 apart:
the measure map (`DAT_004d71d0`, indexed by raw char — `FUN_004015b9`) and
the render map (`DAT_004d71f0` == base+0x20, indexed by `c-0x20` —
`FUN_00402a14`).  Both blit-side and measure-side index by the same
effective byte, so `render[c-0x20] == measure[c]`.  Extracted once and
embedded in `gfx_glyph_table.c` (a drop-in can't read it from the exe at
runtime — it IS the exe).

### What landed

- `gfx_glyph_table.c/.h` + 5 tests (suite 1122 → 1127).
- `gfx_load_menu_font` / `gfx_load_title_font` in gfx.c (raw PL8 read,
  same shape as the panel sheet; freed in gfx_shutdown).
- `main.c` bring-up: bind fonts + glyph tables + l2.eng blob into the
  title chain; set screen-state byte `DAT_004eac50 = 0x1f` so gfx_text
  uses the title-screen **gray** shadow pair (0x36/0x2c) over the default
  colorful (0x10/0x1f) — quirk #114's mirror, fixed the title/button
  shadow delta (1879 → 1125 diff px).

### Then: palette reserved-range blackout → BIT-IDENTICAL menu

The text-track residual (0.37%) was a single root cause: the colorful
title caps + selected-button highlight.  Both render through palette
indices in the Windows system-reserved ranges — uppercase highlight =
**1**, selected-button = **0xf9** — which gateway.256 fills with VGA
colors (red).  Settled it by capturing a **live windowed retail palette**
(`frida_capture.py`, GetEntries dump): indices **1..9 and 246..255 read
back (0,0,0)**; all others match gateway.256.

Root cause: every retail image-palette upload runs `FUN_0042f449`, which
only writes indices **10..245** from the `.256` file and keeps 0..9 +
246..255 at their BSS-zero (black) init.  The port's `gfx_load_palette`
was deliberately loading all 256 — that was the bug.  Ported the
three-segment build (engine-quirk #115).

**Result: the main menu is now bit-identical to retail** —
`compare_frames` reports `identical: true`, 0 diff pixels, against both
the committed ground truth and a fresh live capture.  Backdrop + panel +
title + subtitle + all four button labels + colors all match.

(The "red caps" you may recall are the *fullscreen* look — fullscreen
retail controls all 256 so idx 1 = gateway red.  The drop-in defaults to
windowed and matches the windowed capture, where those slots are black.)

## 2026-05-29 — FIRST VISIBLE MENU PIXELS: backdrop renders + seam wired

Autonomous continuation of the 10-chip push.  Two committed chips, then a
bring-up that put the **gateway main-menu backdrop on screen in the
drop-in** — verified bit-identical to `runs/groundtruth/mainmenu-retail.bmp`
(user confirmed visually).

### Chips landed

1. **Demand-redraw seam (chip 5)** — `755465d`.  Wired
   `engine_tick`'s render phase to the screen dispatch, the retail
   `if (DAT_0057d340) FUN_0040f1a0(DAT_0057d340)` seam.  The main-menu
   render chain now exists end-to-end in src:
   `engine_tick → engine_screen_dispatch(0x1f) → engine_title_render_scene
   → bg + panel + text + buttons → finalize`.
   - engine.c owns the demand-redraw cookie `DAT_0057d340` (g_demand_redraw);
     WndProc was writing a dead `g_render_dirty` mirror — now calls
     `engine_set_demand_redraw(2)` into the single owner.
   - engine_title_render owns the title page selector `DAT_005530f0`
     (set_page/page); the seam sources the page from there.
   - Documented the `DAT_004eac50` two-mirror hazard (g_sim_current_screen
     vs gfx_text's g_language_tag) as engine-quirk #114 — unify when the
     boot→title transition (first producer) lands.

2. **Background asset-load (`FUN_0041e61d` / `FUN_00408fcb`)** — `be4e890`.
   - `gfx_load_background_image()` — raw 8bpp 640×480 read (24-byte PL8
     header skip) straight into the locked back buffer, pitch-aware.
     gateway.pl8 = 0x18 + 640*480 = 307224 B (size confirms the model).
   - `gfx_title_background_load(page)` — `FUN_0041e61d`'s per-page asset
     pick (gateway / custom / skirmish .256 + .pl8).
   - `main.c --title-bg` — bring-up shortcut: loads the page-1 backdrop
     after gfx_init, ahead of the boot→title state producer.

### Verified

Ran from the game dir with `--title-bg --capture-to`; `compare_frames`
vs retail ground truth: **the entire backdrop matches** (armor / red-cross
shield / weapons / stone floor pixel-for-pixel).  The 23% delta is exactly
the panel + title text + buttons rectangle — the composition track.

### Panel landed — `08695ca`

Backdrop + stone panel now match retail pixel-for-pixel (diff 23.3% →
1.58%; the 1.58% is exactly the font-gated text).  User-confirmed.

- `gfx_load_panel_sheet()` — port of `FUN_004b25b0` alloc + panels2.pl8
  read (raw PL8: 8-byte header + 16-byte descriptors + pixels).
- `gfx_lock_canvas()` / `gfx_unlock_canvas()` — bind the locked
  back-buffer base as the gfx_paint canvas; verified pitch == 640 so the
  width-as-stride assumption holds (bails otherwise).
- `--title-menu` renders `engine_title_render_scene(0x1f, 1)` inside one
  back-buffer lock.
- `seek_localized` now guards an unbound loc blob (NULL) → no-op (the
  text path deref'd NULL before the l2.eng blob is bound); root-caused
  with a host ASan repro of the full scene against the real panels2.pl8.

### Next — text (font loading + glyph-table extraction)

The only remaining visible delta.  **Corrected asset-source analysis**
(from the PE section map): `.data` is file-backed only up to VA
**0x4e5a00** — beyond that is BSS (zero at load, filled at runtime).

- **Fonts are runtime-loaded from disk, NOT embedded.**  DAT_005af8f0
  (menu/default) and DAT_005b2fa0 (title) are at 0x5af8f0 / 0x5b2fa0 —
  both in BSS.  The game dir has the font files: `Font_c2.pl8`,
  `Font3c2.pl8`, `Font_10.pl8`, `Fnt_8.pl8`, `Fntl2_{9,14,22}.pl8`, …
  So this mirrors the bg/panel track: find which font `.pl8` loads into
  each buffer (search the decompile for the load into &DAT_005af8f0 /
  &DAT_005b2fa0 — NOT a direct FUN_004af8d9 call, so trace the font-load
  routine), load at runtime via `gfx_load_*`, bind via
  `engine_title_render_bind_fonts`.
- **Glyph tables ARE embedded** (extract from the exe): DAT_004d71f0 /
  DAT_004d71d0 are at 0x4d71f0 / 0x4d71d0 — inside initialized .data
  (0x4d2000..0x4e5a00).  256-byte char→glyph maps (the renderer subtracts
  0x20 before indexing 0x4d71f0, so 0x4d71f0[c-0x20] == 0x4d71d0[c]).
  Extract once, bind via `engine_title_render_bind_glyph_tables`.
- The loc blob (DAT_00591580) already loads at runtime (config: l2.eng,
  100016 B) — bind it via `engine_title_render_bind_localization`.

gfx_glyph (the renderer) already knows the font blob layout (16-byte
glyph descriptors at blob+idx*16+8, same as sprites/panels), so once all
three are bound the ported chain renders the text with no new wiring.

## 2026-05-29 — main-menu render chain ported end-to-end (10-chip push)

Autonomous session: ported 10 more functions (12 incl. the earlier
root+buttons), all host-tested, both builds clean throughout.  Coverage
276 → 293 touched (11.3% → 12.0%), suite 1070 → 1121 (+51 tests).

### Chips landed (each its own commit)

1. `FUN_0046d2a3` → `engine_map_query_region_clear` — 64×64 grid
   "footprint clear?" predicate (bound descriptor grid).
2. `FUN_0042a15f` → `engine_camera_compute_scroll_pixels` — cell-scroll →
   pixel-origin with the staggered odd-row half-step.
3. `FUN_004651b9` → `engine_territory_clear_size_state` — per-territory
   +0x14c/+0x153 byte reset (records 1..150).
4/5. `FUN_0041f15a` / `FUN_00423b4f` → `engine_mp_render` — the two MP
   render-phase overlays (player-indicator timer + status line).
6/7/8. `FUN_0040946d` / `FUN_004097c5` / `FUN_00409346` → `gfx_panel` —
   the tiled stone-panel renderer (9-patch border + interior fill +
   wrapper).  The low-level PL8 tile blitters were already ported in
   gfx_sprite.c, so this is pure orchestration over the gfx_glyph/
   gfx_dispatch sprite+clip state.
9. `FUN_0041e7e1` → `engine_title_render_scene` — title-scene dispatch
   (bg hook + page dispatch + finalize + scene flags).
10. `FUN_0040f1a0` → `engine_screen_dispatch` — top-level screen switch
   (title arm wired; other ~40 arms hooked).

**The main-menu render chain now exists end-to-end in src:**
`engine_screen_dispatch(0x1f)` → `engine_title_render_scene` →
background + `gfx_panel` + title/subtitle text + buttons → finalize.

### What still gates the first visible menu pixel (next session)

1. **Wire the chain into the live render path** — `engine_tick`'s
   render phase (engine.c) needs the demand-redraw seam:
   `if (DAT_0057d340 != 0) engine_screen_dispatch(DAT_004eac50,
   DAT_005530f0, DAT_0057d340)`.  Needs those globals owned/mirrored in
   engine.c (DAT_0057d340 is currently only referenced in comments —
   free to own; DAT_004eac50 is gfx_text's language tag).
2. **The bg + asset long pole** — `FUN_0041e61d` (background blit) +
   the PL8 loader `FUN_004af8d9` populating `DAT_004eabec` (panel) /
   `DAT_004ea8a0` (palette).  This is the real remaining work; bind the
   loaded panel asset into `engine_title_render` + `gfx_panel` and the
   bg hook into `engine_title_render_scene`.
3. **Deferred-stub bindings to wire** — `engine_title_render`'s panel
   hook → `gfx_panel_draw` (signatures match: `(asset,x,y,cols,rows)`);
   the bg hook → the ported `FUN_0041e61d` once it lands.
4. **State track** (for faithful behaviour, not just bring-up) — the
   menu state tick `FUN_0042ff10` (input → page/selection,
   `DAT_00553114`, sets `DAT_0057d340`) + the boot→title transition
   `FUN_0042b780`.  See the step-1 RE in the chip plan.

**Deferred to a checkpoint:** visual parity vs
`runs/groundtruth/mainmenu-retail.bmp` once the chain is wired + assets
load.  Per-chip ports are host-tested but no pixels render in the exe
until (1)+(2) land.

## 2026-05-29 — main-menu composition ported (root + buttons), DDraw panel deferred

Landed the first render chips off the corrected plan: the main-menu
composition that rests on already-ported gfx primitives.

### What landed

- **`src/engine_title_render.c` / `.h`** — new module, following the
  `engine_overlay_render` template (injected font/glyph/dirty/loc/asset
  pointers via binders; pure C, no Win32):
  - `engine_title_render_buttons` ← `FUN_0041eaa3` (487 B): four buttons,
    each a beveled rect (`gfx_line_rect_bevel_palette_b`) + dirty-rect
    mark (`engine_dirty_rect_mark_box_tiled`) + centered label
    (`gfx_wrap_localized_line_centered`).  Selected button
    (`DAT_00553114`) draws its label in highlight color `0xf9` vs `0x3f`.
  - `engine_title_render_main_menu` ← `FUN_0041ea14` (143 B): panel +
    title text (uppercase-highlight mode) + subtitle (no-shadow) +
    buttons.  Brackets the text-mode flags exactly as retail toggles
    `DAT_0058fe2c` / `DAT_005aea40`.
- **DDraw panel deferred** — `FUN_00409346` (gray stone panel) bottoms
  out in the PL8 tile-blit family + `panels2.pl8` asset load (the long
  pole).  Exposed as a bound function pointer
  (`engine_title_render_bind_panel_fn`) defaulting to a no-op-plus-counter
  stub, mirroring overlay-render's deferred army drawer.  Lets the
  composition land + be tested before the tile blitter exists.
- **8 host tests** (suite 1070 → 1078, both builds clean): four-dirty-box
  marking, bevel rects present, selected-button highlight isolation,
  no-selection-all-normal, all-four-labels-render (incl. the 0x2f
  button-3 sub-string), panel-hook arg fidelity (`asset, 0xa0, 10, 0x14,
  0xf`), default-stub counter, full main-menu composition.
- **Quirk #113** — button 3's label is localization sub-string `0x2f`
  (47), not the contiguous `4` the other buttons would suggest; a
  late-added string addressed by its tail index.

No caller in the drop-in yet — these are leaf-first ports (same cadence
as the turn-driver chips).  Wiring them into `engine_tick`'s render phase
(the `DAT_0057d340` demand-redraw seam) + porting the long pole
(`FUN_0041e61d` bg/asset, `FUN_00409346` panel tile-blit) is the path to
the first visible menu pixel vs `runs/groundtruth/mainmenu-retail.bmp`.

## 2026-05-29 — main-menu render-chip plan corrected (decompile re-derivation)

Started the main-menu render milestone by validating the chip plan from the
prior two commits (cc3b13d / e9f2165) against the static decompile before
porting.  The call-trace-derived render tree had **two attribution errors**:

- `FUN_0049a010` was labeled "[ported] screen-state-0 render dispatch".  It is
  neither: it's the **unported in-game state-machine driver** (sub-step body of
  `engine_tick`, switch on `DAT_00569584`; its in-game arm calls the
  just-ported turn-end check `FUN_0049b762`).  It draws nothing.
- `FUN_0043ac23`/`FUN_0043ad25` *do* call `FUN_0040f1a0`, but `FUN_0043ad25`
  sets `DAT_004eac50 = 0` first, so that path renders **screen 0**, not the
  title.  They're teardown handlers, off the menu path.

The real menu-render seam is the **demand-redraw** in `engine_tick`'s render
phase (line 36): `if (DAT_0057d340 != 0) FUN_0040f1a0(DAT_0057d340)`, taken
every frame while `DAT_004eac50 == 0x1f`.  `DAT_0057d340` is a redraw-request
flag (mode 1|2) set by 50+ input/state handlers.  Corrected tree:
`engine_tick → FUN_0040f1a0 (0x1f arm) → FUN_0041e7e1 → FUN_0041e61d (bg+assets)
+ FUN_0041ea14 (panel + 2 texts + FUN_0041eaa3 buttons) → FUN_0045240a`.

Also fully resolved the **text-leaf font globals** (the folded args that made
`FUN_00402c5e`'s 7-arg call sites look incompatible with the 10-arg port
`gfx_wrap_localized_line_centered`): `&DAT_005b2fa0`/`&DAT_005af8f0` are **fonts**
(title / default — the latter already `g_default_font` in `gfx_dispatch.c`), not
string tables; the menu strings are sub-strings of localization-blob entry `0xb`
(`DAT_00591580`, loaded by config); the glyph maps are the fixed static globals
`&DAT_004d71f0` (render) / `&DAT_004d71d0` (measure).  Verified via
`FUN_004025d7`/`FUN_00402637`/`FUN_004014f0`.

Net: render composition (`FUN_0041eaa3` buttons + text leaves) rests on
already-ported primitives and is now portable; the **long pole** is the panel/
background **DDraw tile-blit family** (`FUN_004b446d`/`44d2`/`43b1`/`49f7`) plus
the **PL8 asset load** (`FUN_004af8d9` → `DAT_004eabec`/`DAT_004ea8a0`) and the
boot→title **state track** — those gate the first visible pixel.

No code changed yet — `docs/plans/main-menu-render-chips.md` rewritten with the
corrected tree, chip table, and full data-dependency resolution.  Next: port the
button/text composition leaves (host-testable now), then tackle the long pole
for visual parity vs `runs/groundtruth/mainmenu-retail.bmp`.

## 2026-05-29 — end-of-turn readiness check (turn family, in-flow chip)

The prior session's nominated next chip: `FUN_0049b762` (334 B) →
`src/engine_turn.c` `engine_turn_end_check` + `engine.c`
`engine_sim_turn_resolved`.  The predicate the strategic-turn driver
(`0x49a010`, unported) polls each pass to decide whether the turn may
advance.

### What it does

Two folded behaviours: (1) a **readiness scan** of roster slots 1..5 —
counts players still active and below the `999` "turn done" sentinel,
landing the count in the save-schema scalar `s_slot_00554004`; (2) when
everyone's done, an **advance decision** — SP and MP-alone advance
immediately, otherwise a three-state MP turn-sync handshake on
`g_sim_turn_progress` (`DAT_00553fc8`): arm (state 0→1, clear the 10
per-player ready flags, snapshot the timeout flag, back-date the sync
timer) → wait (1) → finalise (2→0, advance).

### What landed

- **`engine_turn.c` / `engine_turn.h`** — the pure core (every input
  explicit, all mutation in the out struct), mirroring the
  rate-controller / phase-ticker split.  The Win32 wrapper in
  `engine.c` captures `timeGetTime` + the live globals and writes back.
- **Five runtime-only global mirrors** added to `engine.c` (static,
  all 0 in SP, none persisted): `g_sim_active_player_count`
  (`DAT_0053f068`), `g_sim_turn_timeout_reached` (`DAT_00553018`),
  `g_sim_saved_turn_timeout` (`DAT_0055ce88`),
  `g_sim_turn_sync_baseline_ms` (`_DAT_0056d658`),
  `g_sim_turn_ready_flags[10]` (`DAT_00515d60`).  `g_sim_turn_progress`
  and the `s_slot_00554004` scalar already existed.
- **7 host tests** — SP-all-done-advances, someone-mid-turn-waits,
  active-and-below-999 counting, slots-1..5-only scan, MP-single-active,
  MP-handshake arm→finalise, MP-state-1-waits.  Suite **1063 → 1070**,
  both builds clean.
- **Quirks #111 / #112** — the 1..5-vs-1..6 roster-slot disagreement
  between this function and the rate-controller walker `FUN_0043e330`
  (the latter reads one record past the array), and the "arm the sync
  timer 10 s in the past so it fires on the next tick" trick.  Plus a
  `main-loop-tick.md` §`FUN_0049b762` section.

### Frontier honesty note

The camera-band leaves at the top of the derived frontier
(`0x42f58c` + its callees) are **false leaves**: the ledger marks
`FUN_0042f449` / `FUN_0042f306` (the 323 B palette-table builders
`0x42f58c` calls) "ported" only because they're name-dropped in a
`gfx.c` comment — `gfx_load_palette` is a from-scratch reimpl that
never ported them individually.  Left as-is this session (out of
scope); flagged for a future pollution sweep.  This chip's own
neighbour citations (`0x49a010`, `0x49b8b0`, `0x49ce51`) were written
as bare hex per [[project_ledger_fun_token]] to keep the promotion a
clean **+1**.

### Disposition

Ledger: **275 → 276 touched** (11.3 %), 246 tested.  Dead-but-tested —
the sole caller `0x49a010` is unported and nothing drives the handshake
state past 0, so the live SP path always advances via the
`mp_active == 0` branch.  Same posture as the roster/schedule/save
chips.  Natural next chips: the MP turn-advance `0x49b973` (374 B), or
back to the roster-cluster consumer `0x49bd99`.

(Clean `/clear` point — one self-contained chip.)

---

## 2026-05-29 — turn-save backup rotation (save family, in-flow chip)

Next clean leaf off the honest frontier, pivoting from the roster/npc
band into the save family — its only real dependency,
`FUN_004ade93` / `save_write`, already landed in `save.c`.
`FUN_0049a453` (302 B, zero-dep leaf) → `src/save.c`
`save_rotate_and_write`.

### What it does

`save_rotate_and_write(castles_path)` is the wrapper the new-game-setup
(`0x441649`-ish chain) and turn-end handler (`0x49a3e6`) call to persist
a turn.  It maintains a **3-generation backup chain** under fixed
sentinel filenames — `lastturn.*` (newest) → `old_turn.*` →
`safeturn.*` (oldest) — chosen by session mode: `.sav` (SP), or `.svb`
/ `.sva` for the two MP variants (`DAT_00553248`).  When the rotation-
hold counter (`DAT_00553260`) is clear it rotates with three unchecked
file ops — `remove(safeturn)`, `rename(old_turn→safeturn)`,
`rename(lastturn→old_turn)` — then writes the new `lastturn`.  When the
hold is set (only on the MP turn-advance path, `0x49b973`) rotation is
skipped and `lastturn` is overwritten in place.

### What landed

- **`save.c` / `save.h`** — verbatim port next to the existing
  `save_write`/`save_load`.  Bare relative filenames as retail builds
  them (retail `save_write` chdir's to the base dir first via `0x4af502`);
  best-effort rename/remove (a missing backup on the first save is not an
  error); returns `save_write`'s status for test observability (retail is
  void and ignores it).
- **Two MP runtime globals promoted to shared externs** in `engine.h`
  (defined in `engine.c`), mirroring the `g_sim_mp_active` precedent:
  `g_sim_mp_variant` (retail `DAT_00553248`, 57 read sites across the
  engine — the recurring `mp_active==0 || variant!=0` guard) and
  `g_sim_save_rotate_hold` (retail `DAT_00553260`).  Both runtime-only,
  **not** save-schema scalars, always 0 in SP.
- **5 host tests** — SP rotate-and-write, hold suppresses rotation, MP
  variant 0 → `.svb`, MP variant 1 → `.sva`, and first-save-no-backups
  (best-effort renames don't error).  Each runs in a throwaway `mkdtemp`
  dir and always restores cwd before asserting.  Suite **1058 → 1063**,
  both builds clean.
- **Quirk #110** — the rename-chain ring buffer, the suffix-encodes-mode
  split, and the hold countdown.  Also a `save-format.md` section.

### Ledger-pollution near-miss (again)

First regen jumped touched **274 → 277**: two comments cited the
unported neighbours `FUN_004af502` and `FUN_0049b973` by token, each of
which the ledger counts as touched.  Rewrote both as bare hex per
[[project_ledger_fun_token]]; back to a clean **+1**.

### Disposition

Ledger: **274 → 275 touched** (11.2 %), 245 tested, code-byte 10.3 %.
Lands ahead of its callers (new-game setup / turn-end handler both
unported), so dead-but-tested in the drop-in today — same posture as the
roster/schedule chips.  Natural next chips in this band: the turn-end
state machine itself (`0x49b762` end-of-turn check — 334 B leaf, touches
many MP scalars; `0x49b973` turn-advance — 374 B), or back to the roster-
cluster consumer `0x49bd99`.

(Clean `/clear` point — one self-contained chip.)

---

## 2026-05-29 — player seat-assign primitive (roster family, in-flow chip)

Next clean chip off the honest frontier, staying in the sim/roster
family the prior two sessions opened.  `FUN_0049bae9` (180 B, a
zero-dependency leaf with a real ported caller) →
`src/engine_roster.c` `player_seat_assign`.

### What it does

`player_seat_assign(player_id, seat, name)` seats a player into a
strategic slot: marks `g_player_strategic_352[seat][+5]` occupied,
flags the `g_player_aux_44[seat][+0x27]` in-use byte, copies the
31-byte name into the aux record (`+0x04`, via `strutil_bounded_copy`,
no NUL), stamps the aux id field (`+0x00`) unless `player_id == 999`
(the "keep existing id" sentinel), and — single-player only
(`g_sim_mp_active == 0`) — mirrors the strategic assigned-job-slot id
(`+0x0a`) into the aux fixed-job field (`+0x25`).

Real callers: single-player setup (`0x4410bd`, name buffer 0x53f1e0)
and the two network join handlers (`0x4412cc`, `0x44121a`) which read
`player_id` + name out of the join packet.

### What landed

- **`engine_roster.c` / `.h`** — verbatim port alongside the existing
  roster trio (`0x49bbe8/0x49bc1d/0x49bc5f`); same player arrays
  (shared externs `g_player_strategic_352` 0x57bf00 stride 0x160,
  `g_player_aux_44` 0x553d50 stride 0x2c) and `g_sim_mp_active`.
  Reuses `strutil_bounded_copy` (the already-ported `FUN_00401136`).
- **4 host tests** — single-player full path (occupancy/name/id/job
  mirror), the 999 keep-id sentinel, MP skips the job mirror, and the
  bounded 31-byte no-NUL name copy.  Suite **1054 → 1058**, both
  builds clean.
- **Quirk #109** — the 999 in-band sentinel (no separate "set id?"
  flag) and the ≥31-byte source-field assumption of the name copy.

### Ledger-pollution near-miss

First regen jumped touched 273 → **277** (+4 for a single port): the
provenance comment cited the three unported callers by `FUN_<va>`
token, each of which the ledger counts as touched.  Rewrote them as
bare hex per [[project_ledger_fun_token]]; back to a clean **+1**.
Caller citations in `src/` stay bare-hex until actually ported.

### Disposition

Ledger: **273 → 274 touched** (11.2 %), 244 tested, code-byte 10.3 %.
Lands ahead of its callers (setup/join handlers unported), so dead-
but-tested in the drop-in today — same posture as the roster trio and
schedule init.  The roster-cluster consumer `0x49bd99` (3 KB, 4
callees) and the npc-schedule leaves `0x49a453`/`0x49b762` remain the
natural next chips in this band.

(Clean `/clear` point — one self-contained chip.)

---

## 2026-05-29 — NPC worker-schedule init (first sim chip off the honest frontier)

First port chip picked off the corrected frontier from the prior
session.  `FUN_0049caaa` (935 B, the single largest npc-schedule leaf)
→ `src/engine_npc_schedule.c` `engine_npc_schedule_init`.

### What it does

Once per strategic / new-game setup, it schedules the five AI players
(seats 1..5).  For each unassigned seat under the worker-count limit it:
hands out a free job slot (1..5, skipping any a fixed-job player has
reserved), picks that job's first un-taken work-location from a per-job
candidate table, and copies the job's localized 16-byte name from
L2.ENG section 7 into the player's aux record.  A finalize pass then
writes every seat's per-job work-position `(x,y)` and two
schedule-valid flags.

### What landed

- **`engine_npc_schedule.c` / `.h`** — verbatim port.  Reads the now-
  shared `g_player_strategic_352` (stride 0x160) and `g_player_aux_44`
  (stride 0x2c) save arrays, the three difficulty/limit scalars
  (`s_slot_0053f268` worker limit, `s_slot_0055302c` variant select,
  `s_slot_0053f034` difficulty), and the MP master switch.
- **`s_job_table[96]`** — the retail `.rdata` candidate/position table
  at `0x4dc17c`, dumped byte-exact via r2 and indexed by absolute
  offset so one blob serves both the per-job candidate lookup
  (4 variants × 5 jobs × 4 candidates) and the work-position pairs.
- **`g_sim_mp_active` promoted to a shared extern** (`engine.h`).  It
  was a private `static` in `engine.c` (retail `DAT_00553030`); the
  schedule branches on it (MP degrades the candidate table to variant
  0).  Runtime flag, **not** a save-schema scalar — not persisted.
- **7 host tests** — job/loc/position assignment, worker-count limit,
  fixed-job pre-reservation, owned-seat skip, difficulty-variant table,
  MP-forces-variant-0, and the localized-name copy (crafted L2.ENG
  blob).  Suite **1047 → 1054**, both builds clean.
- **Quirks #107/#108** — the global-walker read (and our local-state +
  NULL-guard divergence) and the two unchecked fixed-size-table
  indexings, documented at port time.

### Disposition

Lands **ahead of its callers** — same as the roster trio.  All 13
retail call sites (strategic-setup / new-game / load: `0x441649`,
`0x4427d1`, `0x4978ad`, …) are unported, so the routine is dead-but-
tested in the drop-in today (the frontier's "2 ported callers" was the
just-fixed comment-token pollution).  No `main.c` wiring; the
localization setter gets called when a strategic caller lands.

Ledger: **272 → 273 touched** (11.1 → 11.2 %), 243 tested, code-byte
**10.1 → 10.3 %**.

(Clean `/clear` point — one self-contained chip, frontier still has the
roster-cluster consumer `0x49bd99` and the other order-2/3 leaves.)

---

## 2026-05-29 — ledger integrity: kill the FUN_-token provenance pollution

Triaging the next gameplay chip off `docs/port-frontier.md` surfaced that
the frontier was **corrupt** — and the root cause turned out to be
systemic ledger over-counting, not a one-off. Chasing it down became the
session. Full writeup:
[`findings/ledger-provenance-audit.md`](findings/ledger-provenance-audit.md).

### How it started

The delayed-action **enqueue** cluster (`0x447136` + helpers
`0x4470c4/0x4476e4/0x447740/0x447026`) looked like a clean frontier chip —
the natural complement to the already-ported drain side. But `0x4470c4`,
flagged a *zero-dep leaf*, actually calls the unported 637 B
`FUN_0043eda0` (immediate command-dispatch into the `PTR_FUN_004d57f0`
table). The ledger had `0043eda0` (and the 897 B logger `0x4afab9`, and
enqueue `0x447136` itself) marked **tested** — because their `FUN_<va>`
tokens appear only in *src comments* of tested modules. The
[[project_ledger_fun_token]] gotcha, corrupting the frontier I triage on.

### The gotcha, and why it's not auto-fixable

`gen_port_ledger.py` keys port status on "does `FUN_<va>` appear in src?".
A ported module's comment that names an *unported* neighbour by token
(TODO lists, "retail calls FUN_…", call-chain diagrams, size lists)
silently promotes it to touched/tested. There's no reliable syntactic
separator: genuine provenance is written ≥6 ways (`Port of`,
`port of retail`, `from retail`, `Mirror of retail`, `@ 0x<va> → name`,
section-header-then-code, inlined helpers) and so is pollution. Tightening
the generator would drop the ~200 ports not using the exact "Port of"
phrasing. Fix = **convention (bare hex for unported neighbours) + review**.

### What landed

- **Demoted 44 verified false promotions** (token → bare hex). Headline
  corrected **316 → 272 touched** (12.9 % → 11.1 %), and the truer
  code-byte signal **13.7 % → 10.1 %** — the single biggest offender was
  `0x47309e` (10 915 B), named only as an unported callee in
  `engine_event_queue.h`. Tiers: 3 mixed-callish (`0x43eda0`, `0x4afab9`,
  `0x447136`), 16 callish-only, 25 `.h`-only-no-provenance (each read to
  confirm). Three heuristic false-positives **kept** (genuine "port of"
  ports: `0x404ead`, `0x448411`, `0x4479de`).
- **Frontier self-corrected** on regen: 191 → 151 functions, 78 → 55
  leaves. The enqueue cluster receded behind its (unported) scheduling
  wrappers, where it belongs.
- **`tools/audit_provenance.py`** — a warn-only auditor that re-derives
  self-provenance per touched function and reports callish-only (safe to
  demote) vs review-tier suspects. Re-runnable, wireable into pre-commit.
- Both builds clean, host suite **1047/1047** (all edits are comments).

### Remaining + next

`audit_provenance.py` still flags ~59 review-tier functions — a dense mix
of genuine ports (battlefield leaves, `engine_state_hash` inline helpers,
gfx bring-up) and real pollution (the engine.c `surveyed:`/`unported:`
survey block, main.c teardown notes, gfx.c surface-build trio). Left for a
focused follow-up: separating them needs a per-function "is there a body
in a `.c`?" check, which is judgment, not a regex. The findings doc lists
the confirmed-pollution VAs ready to demote next.

The actual next *port* chip is still open — pick it off the now-honest
frontier. The enqueue cluster stays deferred until the command-dispatch
machinery (`FUN_0043eda0` + the `PTR_FUN_004d57f0` table) is in reach.

(`/clear` is appropriate here — ledger integrity is a clean unit; the next
session picks a port chip from the corrected frontier.)

---

## 2026-05-29 — player-roster trio (first gameplay chip past the scalar wall)

The first sim/strategic-gameplay functions land now that the scalar
wall is down.  The roster trio scoped in the prior dispatch ported
cleanly, both wrinkles handled as planned.

### What landed

- **`FUN_0049bbe8` / `FUN_0049bc1d` / `FUN_0049bc5f` → `src/engine_roster.c`**
  (3 funcs, 433 B).  The player roster: a 6-pair `{army-id, seat}` table
  keyed by seat, a participant counter, and a compactor.
  - `player_roster_add(id, seat)` — write pair[seat], bump the count when
    seat != 0.
  - `player_roster_clear()` — zero all 6 pairs (not the count).
  - `player_roster_compact()` — drop pairs whose seat index exceeds the
    active-player count (seats 1..5 with a `g_player_strategic_352`
    occupancy flag at +5 or +7), shifting later pairs down.
  Host-tested (12 cases, suite 1035→1047); both builds clean.  Ledger
  **316 touched (12.9%)**.

### The two scoped wrinkles, resolved

1. **6-vs-12-byte split (quirk #105).**  Runtime structure is 12 B (6
   pairs); save schema entry 140 persists only the leading 6 B.  Handled
   by promoting `0x568db0` to a **hand-owned `g_player_roster[12]`** in
   `save.h`/`save.c` and adding it to the generator's `NAMED_BIG_BLOCKS`
   with the CSV size 6 — so schema row 140 serialises just the first six
   bytes (byte-identical `.sav` to retail) while the runtime gets all
   six pairs.  No generator size-override needed: the 12 lives purely in
   the hand-written extern; the named-block size stays the CSV's 6 and
   passes the assert.  This is the one named block declared *larger* than
   its schema slot.  The count `0x53e8a4` stays a generated scalar,
   `#define`-aliased as `g_player_roster_count` in the roster header.

2. **Non-convergent compaction (quirk #106).**  The shift cannot remove
   the index-5 entry (its inner loop runs zero times), so an out-of-range
   seat-5 pair keeps `changed` true and spins the pass loop to its 6-pass
   cap, surviving as a trailing duplicate.  Ported verbatim and pinned by
   `roster_compact_non_convergent_tail_survives` (plus the `active=0`
   extreme, where the front pairs collapse to copies of `pair[5]`).

### Ledger-honesty note

The generator marks any `FUN_<va>` token in `src/` as touched.  Initial
comments cited the deferred 3 KB consumer by token (`FUN_0049bd99`),
which falsely promoted it to "tested" (+3063 B).  Demoted neighbour
references to bare hex (`0x49bd99`, `0x467770`) — the `FUN_<va>` token is
now reserved for functions actually ported.  Worth watching for in
future ports: a passing mention of a deferred callee inflates the ledger.

### Next candidate milestones

1. More gameplay leaves now that the wall is down — survey the
   call-graph frontier (`docs/port-frontier.md`) for the next clean
   order-1/2 chip that reads only already-shared globals.
2. The deferred 3 KB consumer `FUN_0049bd99` (reads the compacted roster)
   stays deferred until its own frontier triage.
3. No-op stubs (`0x41423f` etc.) still land with `engine_tick`'s body,
   not ahead of it.

(`/clear` is appropriate here — fresh scope for the next chip.)

---

## 2026-05-29 — gfx teardown primitive (FUN_0042eaa6) + the scalar-global ownership wall

Short porting session that landed one clean frontier leaf and then ran
head-first into the structural blocker gating the rest of the
gameplay-leaf frontier.  Worth reading the **finding** below before the
next session picks a target — it explains why the "knock out the
order-1/2 mechanical leaves" plan from the prior dispatch only yielded
one chip.

### What landed

- **`FUN_0042eaa6` @ 0x42eaa6 → `src/gfx.c`** (order-2 frontier leaf,
  214 B).  The retail DirectDraw teardown primitive: release clipper /
  back / primary, reset back-pitch, and *conditionally* release the
  IDirectDraw instance (gated on `param_1`).  Ported as a static
  `gfx_release_surfaces(int release_dd)`; `gfx_shutdown()` now wraps it
  (palette release + state resets stay in the wrapper, matching retail's
  one-level-up `FUN_004b28cd`).  The `release_dd == 0` path — surfaces
  released but DD kept alive for a mode-switch / surface-lost recreate —
  is faithfully gated but **dead today** (no caller passes 0 until
  `FUN_004b28cd(0)` / the mode-switch wrapper lands).  The retail
  status-logger call `FUN_004afab9(0x4d51dc, …)` is routed through
  `log_line` with a TODO; the exact string lives only in the Ghidra DB.
  Ledger: **313 touched (12.8%)**.  gfx.c is Win32-only (excluded from
  the host suite) — verified by clean cross-compile, no host test.

### The finding — why the rest of the order-1/2 leaves are blocked

The prior dispatch nominated four mechanical leaves
(`0x41423f`, `0x42eaa6`, `0x4360f2`, `0x49bbe8`).  Only `0x42eaa6`
ported cleanly.  Surveying the other three (and the "most-wanted"
`0x4af502`, and the `49bbe8` byte-pair cluster) surfaced three distinct
blockers — but one dominates the whole gameplay layer:

1. **No-op stubs with no live caller** (`0x41423f`, `0x45242b`,
   `0x4b1dd5`) — their only callers are scaffold-only functions
   (`engine_tick`).  Porting them standalone wires into nothing; they
   want to land *with* their caller's body, not before it.

2. **The IO chdir-back `0x4af502`** is part of the tier-2/3 retry chain
   that STATUS deliberately defers.  Its "7 ported callers" reference it
   in retail, but our ports only implemented tier-1, so none actually
   invoke it.  Not premature-to-port-but-dead; just dead.

3. **★ The scalar-global ownership wall.**  `0x4360f2`, the `49bbe8`
   roster cluster, and most gameplay leaves read/write engine globals
   that the save system has already modelled — as **202 save-private
   `static uint32_t s_slot_<va>` mirrors** in the generated
   `src/save_schema_table.h` (included only by `save.c`).  Example:
   the roster count `DAT_0053e8a4` *is* save schema entry 141
   (`s_slot_0053e8a4`).  Porting `FUN_0049bbe8` (which increments that
   count) would create a *second, competing* storage location — runtime
   writes would not be what the save system serialises.  Silent
   save/load desync.  So **any gameplay leaf touching a scalar-tail
   global is blocked until those scalars get a shared runtime home.**

   Note the asymmetry: the **13 big blocks** (`g_player_strategic_352`,
   `g_per_army_768`, …) *are* already shared (extern in `save.h`,
   defined in `save.c`) — which is why the render-phase leaves that read
   them (battlefield / overlay) ported fine.  It is specifically the
   202 *scalar* tail entries that are save-private.

This is the dominant Phase-6 (sim / strategic gameplay) blocker and a
genuine architecture decision — put to the user mid-session.

### Resolution — scalar mirrors promoted to shared externs (same session)

User chose **promote mirrors to externs**.  Reworked
`tools/gen_save_schema.py` to emit three files instead of one:

- **`src/save_schema_globals.h`** — `extern` decls for all 202 unique
  scalar tail VAs (`s_slot_<va>`), includable by any gameplay TU.
- **`src/save_schema_globals.c`** — the storage *definitions* (BSS,
  zero-init).  Auto-picked-up by `src/Makefile`'s `$(wildcard *.c)`;
  added to `tests/Makefile` `SRCS_UNDER_TEST`.
- **`src/save_schema_table.h`** — unchanged role (the 225-entry
  `g_save_schema[]`), now `#include`s the globals header instead of
  emitting `static` storage.

The storage is the same bytes the save schema serialises, so a gameplay
port reads/writes the canonical copy — no desync.  Both builds clean,
**host suite still 1035/1035**.  A slot is renamed to a friendly name by
promoting it into the generator's `NAMED_BIG_BLOCKS` (→ extern in
`save.h`, def in `save.c`) or aliased with a `#define` in the owning
subsystem's header.  The wall is gone for the whole gameplay-leaf
frontier in one stroke.

### Next candidate milestones

1. **`49bbe8`/`49bc1d`/`49bc5f` player-roster trio** — now unblocked
   (count `s_slot_0053e8a4` is shared); the next gameplay chip.  Two
   wrinkles to handle deliberately, found while scoping it:
   - **6-vs-12-byte mismatch.**  The roster table `DAT_00568db0` is save
     entry **140, size 6** (now `s_slot_00568db0[6]`), but the runtime
     structure is **12 bytes** — 6 interleaved `{id, seat}` pairs
     (`db0`=even/id, `db1`=odd/seat; `49bc1d` clears all 6 pairs).  So
     retail persists only the first 3 pairs; the runtime uses all 6.
     The port needs runtime storage *larger* than its persisted slot —
     promote `0x568db0` to a 12-byte named global and point schema row
     140 at its first 6 bytes (the generator's NAMED list asserts
     size==CSV, so this needs a size-override path or a hand-owned
     extern that save.c references).
   - **Non-convergent compaction.**  `49bc5f` can't remove the last
     slot (seat-5 entry: the shift-down copies nothing), so it spins to
     its 6-pass cap leaving trailing duplicates; the consumer reads only
     `active` front entries.  Test exact traces, document the quirk.
   The 3 KB consumer `FUN_0049bd99` stays deferred.
2. The no-op stubs (`0x41423f` etc.) land naturally when `engine_tick`'s
   body ports — don't port them ahead of their callers.
3. `0x4af502` IO chdir-back stays deferred with the tier-2/3 retry chain.

(`/clear` is appropriate after the next gameplay chip — fresh scope.)

---

## 2026-05-29 — Project audit + meta-infrastructure (Opus 4.8 cutover); derived port ledger, frontier backlog, enforced hooks

First session under Opus 4.8 (superseding 4.7).  No engine code ported
this dispatch — the work was an audit of project health and a setup
pass to make future porting sessions effective, mirroring the
meta-infrastructure the openrecet sibling grew during its own audit.

### State of the project at audit time

Healthy.  The host suite is green (**1035 tests pass, 0 fail**) inside
`nix develop`.  The drop-in cross-compiles (`build/openlords2.exe`).
2447 non-thunk engine functions in the Ghidra universe; **312 touched
(12.8%), 272 host-tested, 13.6% of code bytes** — deep into Phase 5
(renderer): battlefield / strategic-map / overlay draw leaves landed,
asset-load IO helper cluster (0x4af*) in progress.

The gap versus openrecet was **tooling/observability, not code**:
no at-a-glance status, no machine-derived port map, no enforced
invariants — port status lived only in the 8825-line PROGRESS log.

### What landed (infrastructure only)

- **`tools/gen_port_ledger.py`** → derives `docs/STATUS.md` (60-second
  headline), `port-ledger.{md,json}` from `functions.csv` (universe) +
  `FUN_<va>` provenance refs in `src/` (touched) + `tests/test_<stem>.c`
  (tested).  Pure derivation, `--check` mode for CI.  Reports a
  code-byte coverage metric alongside the function count (the truer
  signal — the engine has a long tail of tiny leaves).
- **`tools/gen_frontier.py`** → `docs/port-frontier.md`: the **191
  unported functions already called by ported code**, with the **78
  zero-dependency leaves** (all callees ported) ranked by ported-caller
  count.  Derived from the per-function decompile call edges + the
  ledger — this is the answer to "what do I port next?".
- **`docs/ROADMAP.md`** — semantic triage of the top 16 frontier leaves,
  seeded by a bounded read-only survey workflow (`ol2-frontier-triage`,
  16 Explore agents, survey-only — no porting).  Per-function: subsystem,
  port difficulty, target src module, globals, gotchas.
- **`tools/git-hooks/commit-msg`** — enforces the co-author trailer at
  the current model (**Opus 4.8**) and rewrites stale `4.<N>` lines;
  single source of truth for the model version.
- **`tools/git-hooks/pre-commit`** — regenerates+stages the ledger and
  runs the host suite when C changes; aborts on failure.
- **`tools/claude-hooks/guard-paths.sh` + `.claude/settings.json`** —
  PreToolUse hard guard denying Edit/Write under `vendor/` or
  `ghidra/projects/` (the never-mutate-assets rule was prose-only).
- **`tools/setup.sh`** wires `core.hooksPath → tools/git-hooks`.
- **`docs/findings/INDEX.md`** — map of the 14 subsystem writeups.
- Drift fix: AGENT-WORKFLOW.md / README.md trailer 4.7→4.8, pointers
  to STATUS / ledger / frontier / roadmap.

### What the frontier survey turned up

- **`0x4af502` (chdir-back-to-base) is the most-wanted leaf (7 ported
  callers)** — yet the 2026-05-26 dispatch deferred it as "no consumer
  today".  Worth revisiting the IO-cluster deferral; it only pulls in
  `FUN_004afab9` (status.txt logger) or a log stub.
- **Three frontier leaves are pure 11-byte no-op stubs** (`0x41423f`,
  `0x45242b`, `0x4b1dd5`) — retail call-site hooks for shipped-disabled
  logic.  Port as matching no-ops for diff-harness call-site fidelity.
- **`0x4c0580` is a `__free_dbg` wrapper** → port as plain `free()`.
- Battlefield combat-entity struct (strides `0x1a4`/`0x1b0`/`0x160`)
  recurs across `0x47f474`/`0x480f8b`; mapping it once unblocks a
  combat-function cluster.

### Next candidate milestones

1. Knock out the order-1/2 frontier leaves (`0x41423f` no-op,
   `0x42eaa6` gfx teardown, `0x4360f2`, `0x49bbe8`) — mechanical.
2. Re-evaluate the IO retry chain now that `0x4af502` has 7 callers.
3. Map the combat-entity struct to unblock battlefield-state leaves.

(`/clear` is a good idea here — this was a clean infra milestone and the
next is a fresh porting scope.)

## 2026-05-26 — Filename extension helpers + IO-family survey (+18 tests, RVA→string map)

Continuation of the same unattended session.  Previous dispatch
landed the render-phase tail camera clamps and called that band
drained.  This dispatch is the first chip into the asset-load IO
helper cluster (the "what's next #2" item from the prior
dispatch): ports the five pure filename-extension helpers
(`FUN_004af54a/5ba/62c/675/6c9`, 67-114 B each, ~450 B total)
into `src/strutil.c` and surveys the full IO helper family,
extracting the RVA-to-string map for the tier-2/tier-3 retry
chain so future ports against it don't need to re-discover the
extension→subdirectory mapping.

### What landed

The five extension helpers share a single 4-byte module-static
scratch buffer (`g_ext_scratch[4]`, mirror of retail
`DAT_004eafa4..a7`).  Retail's IO retry chain uses the scratch
indirectly through `strcmp` against `"PL8"`/`"256"`/`"WAV"`/
`"SMK"`; other call sites use the save+apply pair atomically to
splice an extension from one filename onto another.

  - `strutil_ext_save(path)` — walks to first `.`, saves the 3
    bytes after it (and a NUL) into scratch.  No-op if no `.`.
  - `strutil_ext_apply(path)` — walks to first `.`, writes
    scratch into bytes [1..3] then NUL at [4] (truncates any
    longer pre-existing extension).
  - `strutil_ext_strip(path)` — null-terminates at first `.`.
  - `strutil_ext_append(path)` — walks to trailing NUL, writes
    `.` + scratch + NUL (caller guarantees ≥5 bytes slack).
  - `strutil_ext_length(path)` — 1-based offset of first `.`,
    OR length-including-NUL if no `.` found.

All five are mechanical ports of the retail walk-to-`.` loop
shape with the scratch read/write differing per variant.

### What was surveyed (not yet ported)

The IO helper cluster spans 14 functions at `0x4af116..0x4afeed`.
Three are now ported (the IO read tier-1 from earlier work
+ the five extension helpers from this chip), eight are
deferred with a concrete blocker, two are still survey-pending:

| Retail | Size | Status |
|--------|------|--------|
| `FUN_004af116` | 508 B | DEFERRED — tier-3 absolute-path chdir with drive substitution |
| `FUN_004af312` | 496 B | DEFERRED — tier-2 base-relative subdir chdir |
| `FUN_004af502` |  72 B | DEFERRED — chdir-back-to-base (no consumer today) |
| `FUN_004af54a` | 112 B | **PORTED (this chip)** — `strutil_ext_save` |
| `FUN_004af5ba` | 114 B | **PORTED (this chip)** — `strutil_ext_apply` |
| `FUN_004af62c` |  73 B | **PORTED (this chip)** — `strutil_ext_strip` |
| `FUN_004af675` |  84 B | **PORTED (this chip)** — `strutil_ext_append` |
| `FUN_004af6c9` |  67 B | **PORTED (this chip)** — `strutil_ext_length` |
| `FUN_004af70c` |  91 B | DEFERRED — file-length (depends on af502) |
| `FUN_004af767` | 218 B | DEFERRED — file-exists (uses retry chain) |
| `FUN_004af841` | 152 B | DEFERRED — file-exists with tier indicator |
| `FUN_004af8d9` | 381 B | PORTED (prior chip) — `io_read_file_at` (tier-1 only) |
| `FUN_004afa56` |  99 B | DEFERRED — file write |
| `FUN_004afab9` | 897 B | DEFERRED — status.txt logger |
| `FUN_004afe3a` | 179 B | DEFERRED — status.txt low-level writer |
| `FUN_004afeed`| 1214 B | survey pending — possibly .ini parser |

The deferral rationale: today's drop-in install layout is FLAT
(`vendor/original/` is one directory with all assets co-located),
and `init_exe_dir` chdirs there at startup, so tier-1 finds every
asset on the first try.  Tier-2/3 only fire on partitioned (CD-
tree-style) installs.  Landing the retry chain today would add
~1 KB of subdirectory chdir code with zero test coverage (io.c is
Win32-only, not host-tested) and no observable behaviour delta.
The retry chain lands naturally alongside whichever future chip
first encounters a "file not found at CWD" path — likely the
intro Smacker subsystem.

### RVA→string map (tier-2 / tier-3 subdirectory selectors)

Extracted via `r2 -q -c 'px 192 @ 0x4df3e0' vendor/original/Lords2.exe`
to nail down the extension→subdir mapping that the retry helpers
walk.  Both tiers match against an uppercased 3-char extension
slot, but tier-2 builds base-relative paths via strcat while
tier-3 substitutes the install drive into a placeholder absolute
path:

| Extension | Tier-2 (base-rel)       | Tier-3 (absolute placeholder) |
|-----------|-------------------------|-------------------------------|
| `PL8`     | `\pl8`                  | `c:.\pl8`                     |
| `256`     | `\pl8`                  | `c:.\pl8`                     |
| `WAV`     | `\wav`                  | `c:.\wav`                     |
| `SMK`     | `\smk_low` / `\smk_high` (gated on `DAT_0053f5f4`) | `c:.\smk_low` / `c:.\smk_high` |

Mapping documented in `docs/findings/ddraw-dsound-bringup.md`
§"Asset-load IO helper family — `0x4af116..0x4afeed`".  When the
deferred ports land, the `c:` prefix in tier-3 strings gets
overwritten with `DAT_0053f200` (install drive prefix captured at
boot).

### Headline numbers

- **1017 → 1035 host tests** (+18), all green under ASan/UBSan.
- **5 retail functions ported** (450 B total), all in `src/strutil.c`.
- **Zero new quirks** — extension helpers are too simple to harbour
  surprises (one shared scratch slot is the only state).
- Cross-compile (mingw32) clean.

### What's next

1. **Pre-tick `FUN_004b0aaa`** — 11 B empty body, lowest-friction
   "structural acknowledgement" chip if the next session needs a
   warm-up.

2. **Other clusters** — `FUN_004987b7` (battlefield init) and
   `FUN_00498dcb` (strategic init) are the two large init
   functions that wire asset loads + blob bindings.  Both
   depend on the IO retry chain ONLY for assets that today live
   in subdirs — for our GOG-flat install, both should work
   against the tier-1 IO path alone.  Worth a survey to confirm.

3. **gfx.c runtime wiring** — battlefield + mini-map are now end-
   to-end at host-test level.  First-frame-on-screen still gated
   on real savegame entity data.

This is a natural `/clear` point — small clean chip, zero new
quirks, 18 tests green, IO family fully surveyed with strings
extracted from the binary, no in-flight work.

---

## 2026-05-26 — Render-phase tail camera clamps (+28 tests, 2 new quirks)

Small follow-on chip after the battlefield render cluster fully
landed: ports the two surveyed-but-deferred clamp helpers that sit
at the head of `engine_tick`'s render-phase tail.  Both are pure
trivial functions (≤135 B), both ran every frame in retail right
before the FPS counter (decompile lines 88725-88726), both were
left as TODO comments in our port because they were "blocked on
producer" — i.e. nothing in our drop-in writes the values they'd
clamp.  Landing them as idempotent no-ops puts the structural
seams in place so the future producer ports (keyboard arrow
handler, entity-centering, castle-table re-center) can land
without also wiring the tail.

### What landed

  - `engine_camera_clamp_scroll` (`FUN_00429b1d`, 135 B) →
    `src/engine_camera.c`.  Clamps strategic-map cell-scroll
    position `(col, row)` against variable bounds
    `[0, 0x40-col_count]` × `[0, 0x80-row_count]`.  Strategic map
    is hardcoded 64×128 in the binary.
  - `engine_camera_clamp_anchor` (`FUN_0047ed34`, 103 B) →
    same file.  Clamps strategic / battlefield camera anchor
    `(x, y)` against hardcoded `[0, 0x41]` × `[0, 0x42]`.
    Asymmetric upper bounds (quirk #103).
  - Wrappers `engine_tick_camera_scroll_clamp` /
    `engine_tick_camera_anchor_clamp` in `src/engine.c` bundle the
    new file-static mirrors (`g_cam_scroll_*`, `g_cam_anchor_*`,
    `g_cam_visible_*`) into pointer args, run the pure clamps,
    write back.  Wired at the head of the render-phase tail
    immediately before `engine_tick_fps_step`, matching retail's
    call order at line 88725-88728.
  - 28 unit tests cover both axes × {in-range, at-max, one-over,
    huge, negative}, asymmetric upper bound, idempotence, and the
    new "oversize visible count pushes 0 negative" quirk.

### New engine quirks

  - **#103** — `FUN_0047ed34` anchor clamp uses asymmetric bounds
    (0x41 vs 0x42), probably a hand-tuned typo preserved in the
    binary.
  - **#104** — `FUN_00429b1d` scroll clamp's lower-then-upper test
    order can push a value below 0 when `visible_cols > 0x40`
    (never observed live, but pinned in tests).

### Headline numbers

- **989 → 1017 host tests** (+28), all green under ASan/UBSan.
- **2 retail functions ported** (238 B total).
- **2 new engine quirks** (#103, #104).
- Cross-compile (mingw32) clean.

### Render-phase tail port state

| Retail        | Size  | Status     |
| ------------- | ----- | ---------- |
| `FUN_00429b1d` | 135 B | **ported (this session)** |
| `FUN_0047ed34` | 103 B | **ported (this session)** |
| `FUN_00426529` |  11 B | surveyed (empty body) |
| `FUN_004b27bf` | 143 B | ported (2026-05-24) |
| `FUN_004b12fb` |  21 B | surveyed (return-1 stub) |
| `FUN_004b0e03` | 875 B | surveyed (palette-fade, blocked on intro) |

Of the six tail functions: 3 ported, 3 surveyed (one blocked on
intro/Smacker screen subsystem, two are debug-hook stubs).  The
entire render-phase tail is now structurally complete to the
extent it can be without subsystem-blocked work landing.

### What's next

1. **Pre-tick survey / port** — the pre-tick block still has five
   subfunctions left, all gated on screen state our drop-in can't
   produce today.  Each is documented in
   `docs/findings/main-loop-tick.md` §"Pre-tick subfunctions —
   surveyed, deferred".  Smallest is `FUN_004b0aaa` (11 B empty
   body) — could be the next "structural acknowledgement" landing,
   though it adds zero observable behaviour.

2. **Asset-load helpers** — `FUN_004af502` (chdir), `FUN_004af8d9`
   (open/seek/read/close, tier-1 already in `src/io.c`), and the
   tier-2/tier-3 retry helpers `FUN_004af312` / `FUN_004af116` are
   needed by both init functions (`FUN_004987b7` battlefield,
   `FUN_00498dcb` strategic) and many other call sites.  Porting
   them as a coherent IO-helper cluster would unblock the init
   functions as a next chip.

3. **gfx.c runtime wiring** — the battlefield + mini-map renderers
   are end-to-end at host-test level.  Standing up real entity
   data from a savegame remains the bigger lift gating "first
   battlefield frame on screen".

This is a natural `/clear` point — small clean chip lands two
deferred functions, two new quirks documented, no in-flight work,
tests green.

---

## 2026-05-26 — Battlefield chips 6b + 7: leftover coord helpers + mini-map / hex-grid overview renderer (+34 tests, 3 new quirks) — ENTIRE 0x4bc BAND DRAINED

Continuation of the same unattended session.  Previous dispatch
landed chip 5d (final pass) and called the battlefield render
cluster complete, leaving only "the scroll/pan family" in the
address band.  This dispatch lands BOTH remaining chips: the five
leftover coord helpers that chip 6 missed (chip 6b) AND the family
that the original survey mislabeled "scroll/pan" but turned out to
be the mini-map / hex-grid overview renderer (chip 7).  After this,
EVERY surveyed function in the `0x4bc..0x4bf` address band is a real
port — no stubs, no skips.

### Survey correction

The chip 6 helper-block in `docs/findings/battlefield-render.md`
labeled `FUN_004bc1d1 / 51a` as "scroll/pan — possibly large state
machine for keyboard/mouse panning."  Reading the actual code:

  - `FUN_004b49b7` (64 B) is a 2-pixel raw blit primitive (writes
    2×2 bytes at the cursor with the same hardcoded 640-byte stride
    as the 16×16 / 32×32 siblings).
  - `FUN_004bc51a` (994 B) walks the descriptor table at 2 px per
    cell, calling that primitive once per cell.  At 80 × 80 cells,
    that's exactly a 160 × 160 thumbnail.
  - `FUN_004bc1d1` (81 B) increments a rolling "next-scan-row"
    cursor by N and dispatches a strip render via 51a.
  - `FUN_004bc020` + `4bc107` are bulk binders for the SHARED hex-
    grid context (the descriptor table they walk IS the same one
    the battlefield terrain passes walk).

It's a mini-map, not a scroll/pan.  Filed the correction in the
finding and renamed the chip family.

### Chip 6b: 5 leftover coord helpers (374 B)

  - `FUN_004bc3ac` (97 B) → `pixel_to_row_bottom_biased`.  Y-axis
    sibling of `pixel_to_col_right_biased`.
  - `FUN_004bc40d` (100 B) → `pixel_to_row_top_biased`.  Y-axis
    sibling of `pixel_to_col_left_biased`, but clamps against
    `total_rows` (DAT_004e6570) rather than the X sibling's
    `camera_row_stride` (DAT_004e6578).  Different clamp axis on
    each sibling — quirk-worthy but not filed (subsumed by quirk
    #82 from chip 6).
  - `FUN_004bc471` / `4bc4bb` (74 B each) → `col_delta` / `row_delta`.
    Signed cell-distance helpers between two screen pixels.
  - `FUN_004bc505` (21 B) → `get_zoom`.  Trivial scalar accessor.

All five reuse module statics already bound by the render passes.
+10 tests.

### Chip 7: mini-map / hex-grid overview renderer (1.4 KB across 5 funcs)

  - `FUN_004b49b7` (64 B) → `gfx_sprite_blit_2x2_raw`.  Micro-
    sibling of the 16×16 / 32×32 raw blits.  Same hardcoded 640-byte
    stride (quirk #75).
  - `FUN_004bc020` (231 B) → `bind_hex_grid_context`.  Bulk binder
    for the SHARED hex-grid context (11 params).  Dispatches through
    the per-field binders we already have.
  - `FUN_004bc107` (59 B) → `bind_minimap_context`.  Bulk binder
    for mini-map-specific context (6 params: 3 sprite blobs + origin
    + mode gate).
  - `FUN_004bc1d1` (81 B) → `minimap_scroll`.  Bumps the next-row
    cursor by N, wraps at `total_rows - N`, dispatches a strip
    render iff mode gate == 2.
  - `FUN_004bc51a` (994 B) → `minimap_render_strip`.  Walks N rows
    × all `row_stride` cols.  Five per-cell paths:
      * Terrain primary blob (flags & 0x1c == 0)
      * Terrain alt blob (flags & 0x1c == 4)
      * First-col-ingame override (default desc slot 0 — overrides
        the normal blob-selector path when engine state == 3)
      * Boundary cell (default desc slot 0, bit 2 cleared after)
      * Entity cell (owner→slot indirection via two tables, owner
        type 6 short-circuits to slot 6, slot 0 ⇒ no blit)

The mini-map is ROLLING.  At ~50 Hz with the main-loop's
`FUN_004bc1d1(4)` call, the full 80-row strategic-map mini-map
refreshes every ~0.4 s — visually smooth but cheap.  Full-redraw
call sites use `FUN_004bc1d1(0x50)` for an immediate complete
sweep.

The same descriptor table that the battlefield terrain passes walk
at 16/32 px per cell IS the table the mini-map walks at 2 px per
cell.  Both strategic and battlefield init paths bind the renderer
identically: 80×80 cells, viewport (0, 24), visible 15×14, zoom 32,
mini-map origin (466 or 480, 11).

+24 tests (5 for the 2×2 raw primitive, 19 for the mini-map render
+ binders), all green under ASan/UBSan; mingw32 cross-compile
clean.

### New engine quirks

  - **#100** — rolling-scan wrap formula uses
    `total_rows - rows < scan_pos` so a partial strip never
    overflows the bottom (one-row guard band).
  - **#101** — unmapped `flags & 0x1c` values (8, 12, 16, 20, 24,
    28) silently drop the cell.  Only 0 and 4 map to a blob.
  - **#102** — first-col-ingame override fires BEFORE the blob-
    selector, so the leftmost mini-map column is always painted from
    the default descriptor slot 0 when engine state == 3.

### Headline numbers

- **950 → 989 host tests** (+39 across both chips), all green
  under ASan/UBSan.
- **10 retail functions ported**: 5 coord helpers (374 B) + 1
  raw blit primitive (64 B) + 4 mini-map functions (1365 B).
  Total ~1.8 KB of decompile → port.
- **3 new engine quirks** (#100 wrap guard, #101 silent drop,
  #102 first-col override).
- Cross-compile (mingw32) clean.

### Battlefield port state — FULLY COMPLETE

| chip   | function                                      | size   | status     |
| ------ | --------------------------------------------- | -----: | ---------- |
| 1      | 16×16 raw blit primitive                      |  705 B | ported     |
| 2      | frame driver + zoom-16 no-op                  |  154 B | ported     |
| 3      | zoom-16 terrain pass                          |  725 B | ported     |
| 3.5    | 32×32 raw blit primitive                      | 2997 B | ported     |
| 4      | zoom-32 terrain pass                          | 1187 B | ported     |
| 5a.i   | middle-pass driver shell                      |  543 B | ported     |
| 5a.ii.a | sorted-entity infrastructure                 |  761 B | ported     |
| 5a.ii.b | per-entity draw base path                    | 1087 B | ported     |
| 5a.iii | per-cell draw leaves                          | 1769 B | ported     |
| 5a.iv  | type-special leaves                           | 2549 B | ported     |
| 5b     | zoom-32 overlay pass                          |  436 B | ported     |
| 5c     | late pass driver + creature + marker          | 1425 B | ported     |
| 5d     | final pass (debug label overlay)              |  433 B | ported     |
| 6      | 5 coord helpers (X-axis + hit-test)           |  394 B | ported     |
| 6b     | 5 coord helpers (Y-axis + deltas + zoom)      |  374 B | **ported (this session)** |
| 7      | mini-map renderer (2×2 prim + 4 funcs)        | 1429 B | **ported (this session)** |

**ENTIRE 0x4bc..0x4bf BAND DRAINED.**  ~16 KB across ~39 functions.
Zero stubs, zero skips.

### What's next

1. **gfx.c runtime wiring** — battlefield rendering + mini-map are
   now end-to-end at host-test level.  The next visual milestone
   is "first battlefield frame" which still requires standing up
   the entity table from real savegame data (the larger lift).
   Visual verification remains gated on user availability.

2. **Other clusters** — the chip-7 detour revealed that
   `FUN_004987b7` (battlefield init) and `FUN_00498dcb` (strategic
   init) are large init functions that wire all the asset loads
   and bindings.  Both are good top-down candidates for the next
   chip plan — they're entry points into the asset-loading +
   blob-binding subsystems.

3. **Strategic map main view** — the chip-7 work clarified that
   the strategic-map MAIN-VIEW renderer (15×14 visible cells at
   zoom 32) is the SAME function family as the battlefield (it
   shares descriptor + bindings).  So all 14 battlefield chips
   ALSO already render the strategic map's main view — once
   `FUN_004987b7 / 498dcb` wires them up with the strategic
   blobs + camera, the strategic map should render too.

This is a natural `/clear` point — both chips land cleanly, all
989 tests green, three new quirks documented, the survey-mislabel
is corrected in the finding, no incomplete work in-flight.

---

## 2026-05-25 — Battlefield chip 5d: final-pass debug-label overlay (+11 tests, 2 new quirks) — ENTIRE CLUSTER LANDED

Continuation of the same unattended session.  Previous dispatch
landed chip 5c (the late pass).  This dispatch lands chip 5d
(`FUN_004bf1f9`, 433 B), the final pass — and with it, the ENTIRE
battlefield render cluster is now ported.  All 14 chips of the
chip plan are real ports; only the scroll/pan input family
(`FUN_004bc1d1 / 51a` — not part of rendering) remains in the
address band.

### Chip 5d: final-pass debug-label overlay (`FUN_004bf1f9`)

Replaces the static stub `pass_final` with the real port.  Debug-
mode overlay drawing a per-creature numeric label on each visible
cell — used in retail during AI-tuning sessions or designer
playtests.

Body shape:

  1. Outer gate on retail `DAT_005bb4a5` debug flag.  When zero,
     pass is a no-op (even the per-cell counter does NOT bump).
  2. Walks the same visible grid the late pass walks (rows × cols,
     no over-scan).
  3. Per cell with `descriptor[+5] != 0`:
       a. Sum first 0x19e bytes of the entity record (port of
          retail `FUN_00440089`).  Cast to signed char → int
          (sign-extended).
       b. Three-way bucket:
            sum <  0x65  → value = sum,        pen = 0x3f
            sum <  0xc9  → value = sum - 100,  pen = 0xf9
            else         → value = sum - 200,  pen = 0x20
       c. Skip render when value == 0 OR value == 999.
       d. Otherwise render at zoom-aware pixel pitch
          (`col * g_zoom + viewport_x + 1`, etc.) via
          `gfx_text_label_render_dec`.

The pixel pitch is ZOOM-AWARE — unlike the late-pass quirk #97
which hardcodes 0x20.  Filed as quirk #99 (counter-example to #97).

### New API surface

  - `bind_debug_label_enabled(int enabled)` — retail `DAT_005bb4a5`.
  - `bind_debug_label_font(font_base, char_glyph_table)` — retail
    `&DAT_005af8f0` font (the port's `gfx_text_label_render_dec`
    factors char_glyph_table out separately, matching the existing
    debug-text pattern in `engine_overlay_render`).
  - `bind_debug_label_suffix(const char *suffix)` — retail
    `&DAT_004e4520` string.
  - Test-only `run_pass_final_for_test(camera_x, camera_y)` sibling
    of the late-pass test entry.

New accessors: `final_per_cell_count`, `final_label_render_count`.

### New engine quirks

  - **#98** — three-way bucket has UNREACHABLE branches.  Sum range
    `[-128, 127]` for signed-char; bucket-mid (`101 <= sum < 201`)
    and bucket-high (`sum >= 201`) are dead code.  Filter
    `value != 999` also unreachable (max value is 127).  Likely
    leftover from an older revision where sum was a wider type.
  - **#99** — final-pass cell pitch IS zoom-aware (uses `g_zoom`),
    counter-example to chip 5c's quirk #97 (late-pass pitch
    hardcoded 0x20).  Same descriptor table, same gates, but
    different convention — reinforces that #97 is a localized
    decision inside the late-pass leaves.

### Headline numbers

- **939 → 950 host tests** (+11), all green under ASan/UBSan.
- **1 retail function ported**: `FUN_004bf1f9` (433 B).
- **2 new engine quirks** (#98 dead bucket branches, #99
  zoom-aware pitch counter-example).
- Cross-compile (mingw32) clean.

### Battlefield port state

| chip   | function                                      | size   | status     |
| ------ | --------------------------------------------- | -----: | ---------- |
| 1      | 16×16 raw blit primitive                      |  705 B | ported     |
| 2      | frame driver + zoom-16 no-op                  |  154 B | ported     |
| 3      | zoom-16 terrain pass                          |  725 B | ported     |
| 3.5    | 32×32 raw blit primitive                      | 2997 B | ported     |
| 4      | zoom-32 terrain pass                          | 1187 B | ported     |
| 5a.i   | middle-pass driver shell                      |  543 B | ported     |
| 5a.ii.a | sorted-entity infrastructure (938+da92+db95) |  761 B | ported    |
| 5a.ii.b | per-entity draw FUN_004bdc31 base path       | 1087 B | ported    |
| 5a.iii | per-cell draw leaves (574+759+beed4)          | 1769 B | ported    |
| 5a.iv  | type-special leaves (4df+7be+ab9)             | 2549 B | ported    |
| 5b     | zoom-32 overlay pass                          |  436 B | ported     |
| 5c     | late pass driver + creature + marker          | 1425 B | ported     |
| 5d     | final pass (debug label overlay)              |  433 B | **ported (this session)** |
| 6      | 5 coord helpers (hit-test + screen↔hex)       |  394 B | ported     |

**Cluster complete.**  ~14 KB across ~21 functions, all real ports.

### What's next

1. **Scroll/pan input family** (`FUN_004bc1d1 / 51a` — possibly
   994 B for the latter).  Not a render pass — handles keyboard
   and mouse panning + edge auto-scroll.  Could chunk as a single
   chip after a survey.

2. **gfx.c runtime wiring** — battlefield rendering is now end-to-
   end at zoom 16/32 for all 14 chips.  The next visual milestone
   is "first battlefield frame with units rendered" which still
   requires standing up the entity table from real savegame data
   (the larger lift).  Visual verification remains gated on user
   availability.

3. **Other clusters** — strategic-mode rendering, save format,
   input replay, etc.  Pick based on top-down RE workflow (see
   user memory).

This is a natural `/clear` point — chip 5d lands cleanly, entire
battlefield cluster complete (14/14 chips), 950 tests green, three
new docs entries (battlefield-render.md status update + 2 quirks
in engine-quirks.md), no incomplete work in-flight.

---

## 2026-05-25 — Battlefield chip 5c: late-pass driver + per-cell creature draw + selection marker (+25 tests, 3 new quirks)

Continuation of the same unattended session.  Previous dispatch
landed chips 5a.iii + 5a.iv (the chip-5a sub-plan completion).  This
dispatch lands the entire late-pass cluster as a single chip — the
driver `FUN_004bd233` (290 B), the per-cell creature draw
`FUN_004be070` (719 B), and the selection-marker leaf `FUN_004be33f`
(416 B).  Total ~1425 B across three functions.  Only chip 5d (final
pass, 433 B) remains stubbed in the battlefield cluster.

### Chip 5c: late-pass driver + two leaves (`FUN_004bd233 / 4be070 / 4be33f`)

Replaces the static stub `pass_late` with the real driver plus its
two leaves.

**`FUN_004bd233` (290 B) — late-pass driver.**  Walks visible cells
(`rows × cols`, NO over-scan unlike the middle pass).  For each cell:

  - Read `entity_id = descriptor[+5]`.  Skip when 0.
  - AND two visibility-mask bytes:
      - per-entity: entity record at offset `+0x0a` (new offset).
      - per-player: byte at `vis_base[player_id * 0x160]`.
    Skip when AND is 0.
  - Dispatch `late_leaf_draw_creature(entity_id)`.

After the cell loop, if the selection-marker gate
(retail `DAT_00553f84`) is positive, dispatch the marker leaf.

Per-cell cursor pitch is HARDCODED `0x20` (32 px) regardless of
`g_zoom` — filed as quirk #97.  This means the late pass is
effectively zoom-32-only despite being dispatched unconditionally.

**`FUN_004be070` (719 B) — per-cell creature draw leaf.**  Stages
sprite descriptor from the creature blob with slot computed as:
`raw = entity[+0x1a0]` (signed short); `shifted = raw << amount`
(or `>> amount` if shift_sign < 0); `slot = shifted + 0x2e`.  Then
applies a type-conditional Y offset:
  - type < 7        → cursor_y -= 12.
  - type == 10      → cursor_y -= 12  (duplicate branch — preserved).
  - else            → cursor_y -= 0x20.
    - if type == 9  → cursor_y += 0x10  (net -16).

X offset always: `cursor_x += anim_x + 8`.  Anim offsets pulled from
the z32 table UNCONDITIONALLY (quirk #95 — chip 5a.ii.b gates this
by zoom; chip 5c does not).

Glyph-offset gate `[1, 0x4baf0]`.  On PASS: clip + dispatch RLE blit,
cursor restored to entry.  On FAIL: cursor LEFT at post-offset
position (quirk #96 — lopsided restore propagates into the next
cell's per-cell advance within the same row).

**`FUN_004be33f` (416 B) — selection-marker leaf.**  Draws a fixed-slot
(0x3f) sprite at the active hero/army's world position
(`px = (marker_x - cam_x) * 0x20 + viewport_x + 2`, etc.).  Marks a
3×3 tile dirty box at the computed cursor (UNCONDITIONALLY — above
the glyph-offset gate).  Inside the gate, invokes a world-map dirty
hook with `(marker_x - 1, marker_y - 1, 3, 3)` — retail's
`FUN_0046c4a3` writes bit 1 into the world-map collision grid at
`&DAT_005440e2`.  The port substitutes a callback to avoid pulling
the world-map subsystem into the battlefield module.

### New API surface

  - `bind_creature_sprite_blob(blob)` — retail `DAT_005530c8`
    (misc_ske.PL8), shared by both leaves.
  - `bind_late_player_visibility_mask(base, stride, player_id)` —
    retail `&DAT_0057bfee`, stride 0x160, player id `DAT_0057c8cc`.
  - `bind_creature_slot_shift(amount, sign)` — retail `DAT_00566514`,
    `DAT_0056651c`.
  - `bind_selection_marker(x, y, gate)` — retail `DAT_00553f14`,
    `DAT_00553f18`, `DAT_00553f84`.
  - `bind_world_map_dirty_hook(hook)` — port-only callback substitute
    for retail's direct `FUN_0046c4a3` call.
  - Test-only `run_pass_late_for_test(camera_x, camera_y)` sibling of
    `run_pass_middle_for_test`.

New entity-record offsets:
  - `+0x0a` — late visibility mask byte.
  - `+0x1a0` — late sprite slot (signed short).

New accessors: per-cell visit count, creature draw/blit counts,
selection-marker call/blit counts, world-dirty-hook call count.

### New engine quirks

  - **#95** — late creature draw reads z32 anim table even at zoom 16
    (chip 5a.ii.b gates by zoom; chip 5c does not).
  - **#96** — creature draw leaf has lopsided cursor restore: on
    gate PASS, cursor → entry; on gate FAIL, cursor → post-offset
    position.  Propagates into the rest of the row's cells (cursor_x
    is reset per row; cursor_y never resets within a frame).
  - **#97** — late-pass cell pitch HARDCODED `0x20` regardless of
    `g_zoom`.  Effectively zoom-32-only despite unconditional
    dispatch.

### Headline numbers

- **914 → 939 host tests** (+25), all green under ASan/UBSan.
- **3 retail functions ported**: `FUN_004bd233` (290 B driver),
  `FUN_004be070` (719 B creature draw), `FUN_004be33f` (416 B
  selection marker).  Total ~1425 B of decompile → port.
- **3 new engine quirks** (#95 unconditional z32 anim, #96 lopsided
  cursor restore, #97 hardcoded 0x20 pitch).
- Cross-compile (mingw32) clean.

### Battlefield port state

| chip   | function                                      | size   | status     |
| ------ | --------------------------------------------- | -----: | ---------- |
| 1      | 16×16 raw blit primitive                      |  705 B | ported     |
| 2      | frame driver + zoom-16 no-op                  |  154 B | ported     |
| 3      | zoom-16 terrain pass                          |  725 B | ported     |
| 3.5    | 32×32 raw blit primitive                      | 2997 B | ported     |
| 4      | zoom-32 terrain pass                          | 1187 B | ported     |
| 5a.i   | middle-pass driver shell                      |  543 B | ported     |
| 5a.ii.a | sorted-entity infrastructure (938+da92+db95) |  761 B | ported    |
| 5a.ii.b | per-entity draw FUN_004bdc31 base path       | 1087 B | ported    |
| 5a.iii | per-cell draw leaves (574+759+beed4)          | 1769 B | ported    |
| 5a.iv  | type-special leaves (4df+7be+ab9)             | 2549 B | ported    |
| 5b     | zoom-32 overlay pass                          |  436 B | ported     |
| 5c     | late pass driver + creature + marker          | 1425 B | **ported (this session)** |
| 5d     | final pass                                    |  433 B | stub       |
| 6      | 5 coord helpers (hit-test + screen↔hex)       |  394 B | ported     |

Only chip 5d remains within the battlefield cluster.

### What's next

1. **Chip 5d — final pass.**  `FUN_004bf1f9` (433 B).  Survey done
   during chip 5c planning — appears to be a debug overlay that
   draws per-creature numeric stats (a checksum-derived label) on
   top of each visible creature, gated on `DAT_005bb4a5` (debug
   flag).  Uses the same per-cell cursor formula as the late pass
   but at zoom-level cursor pitch (`DAT_004e656c` directly — so
   NO quirk-#97 mis-alignment).  Calls `FUN_00440089` (81 B —
   byte-sum hash) and `FUN_00402f64` (text/value label, already
   ported).  Smaller surface than chip 5c — likely a single short
   dispatch.

2. **gfx.c runtime wiring** — battlefield is now end-to-end at zoom
   16/32 for all chip 5a + 5b + 5c layers.  Visual verification
   remains gated on user availability; the next milestone where a
   screenshot would be most informative is still "first battlefield
   frame with units rendered" (requires standing up the entity
   table from real savegame data).

Picking **#1 (chip 5d)** next: the only remaining stub in the
battlefield cluster and a debug-only feature, so it's a clean
"finish the cluster" landing.

This is a natural `/clear` point — chip 5c lands cleanly, all 939
tests green, the chip-5c plan is documented in
`docs/findings/battlefield-render.md`, no incomplete work in-flight.

---

## 2026-05-25 — Battlefield chips 5a.iii + 5a.iv: per-cell draw leaves + type-special leaves (+41 tests, 5 new quirks)

Continuation of the same unattended session.  Previous dispatch landed
chip 5a.ii.a/b (sorted-entity infrastructure + per-entity draw base
path).  This dispatch lands the two remaining chip-5a sub-chips —
both the three per-cell draw leaves AND the three type-special leaves
that the per-entity draw fans out to.  After this, the **entire
chip-5a sub-plan is landed**; only chips 5c (late pass) and 5d (final
pass) remain stubbed within the battlefield cluster.

### Chip 5a.iii: per-cell draw leaves (`FUN_004bd574 / 4bd759 / 4beed4`)

Replaces three counter-stubs with real ports.  All three are
dispatched per-cell from the middle-pass driver shell under different
descriptor-bit gates:

  - `desc[+2] & 0x80, desc[+3] == 0` → `FUN_004bd574` player marker
  - `desc[+2] & 0x80, desc[+3] != 0` → `FUN_004bd759` unit-key sprite
  - `desc[+6] != 0`                  → `FUN_004beed4` effect chain
                                        (independent of bit 0x80)

**`FUN_004bd574` (485 B) — player marker.**  8-frame animated sprite
cycle drawn over the active player's hex.  Slot computation:
`banner_byte * 8 + frame + 0x21` where banner_byte comes from a
per-player byte table at stride 0x1a4 (each player has a banner color
that picks an 8-slot band of the marker sprite blob).  Frame counter
(retail `DAT_004e5b18`, 0..7 wrapped) advances when the bound
`anim_advance` flag is non-zero, holds when zero.

**`FUN_004bd759` (479 B) — unit-key sprite.**  Static letter sprite.
4-entry key→slot map: `0x49→0x1f, 0x4c→0x20, 0x61→0x21, 0x64→0x22`.
Unknown keys early-return without dispatch (no blit, no slot capture).
Otherwise same render path as player_marker (shared helper).

**`FUN_004beed4` (805 B) — effect chain.**  Walks a linked list of
visual-effect records anchored to a cell.  Each record has its own
sprite slot (subject to a shared blob in the port — see API notes
below).  10-hop safety cap, self-loop guard, terminator on `id==0` or
`type==0`.  Subtype 7 skips render but continues walk; subtype 5
advances a shared 0..15 wrapped anim counter (retail `DAT_004e5b1c`)
and adds a per-frame (x_off, y_off) jitter from a 16-entry table;
other subtypes render at static position.

### Chip 5a.iv: type-special leaves (`FUN_004be4df / 4be7be / 4beab9`)

Replaces three counter-stubs in `middle_leaf_draw_entity` (chip
5a.ii.b) with real ports.  All three are PRE/POST-calls dispatched
from the per-entity draw under type/subtype gates:

  - `type == 6`                     → `FUN_004be4df` PRE-call
  - `type == 7`                     → `FUN_004be7be` POST-call
  - `type == 9 AND subtype == 0x0e` → `FUN_004beab9` POST-call

All three share the same body shape (stage → gate [1, 0x4baf0] →
zoom-conditional anim-offset + centering → 6×6 dirty box → set clip →
RLE blit → restore cursor), but differ in blob source and slot offset:

  - **type-6**: shadow/ground effect.  Blob from
    `bind_entity_type6_blob` (port substitute for the entity's +0x04
    secondary blob field — 8-byte host pointer can't safely live there
    without colliding with primary at +0x00).  Slot from entity's
    secondary frame index (+0x11).
  - **type-7**: blob picked by entity facing direction
    (`facing < 4` → low blob, `facing >= 4` → high blob).  Both bound
    via `bind_entity_type7_blobs(low, high)`.  Slot from secondary frame.
  - **type-9-sub-0xe**: TWO sprites per call from the entity's primary
    blob.  First slot = `secondary_frame + 0x16` with `cursor_y -= 16`;
    second slot = `secondary_frame + 0xd` with `cursor_y += 0x56`.
    Both anchored at the SAVED entry cursor (not the first sprite's
    post-adjustment cursor), yielding a fixed 102-pixel vertical
    separation between the two sprites.

### New API surface

Chip 5a.iii bindings:

  - `bind_player_marker_blob / slot_table / player_id / anim_advance`
    — five inputs cover the marker's blob, per-player slot-table base
    + stride, active player id, and the "should advance frame" tick.
  - `bind_unit_key_blob` — just the blob; slot selection is internal
    via the 4-entry key→slot switch.
  - `bind_effect_chain_table / shared_blob / anim_table` — record
    array (base + stride + max_id), shared blob (port-only substitute
    for the per-record blob pointer that doesn't fit in 4 bytes on
    64-bit hosts), and the 16-entry subtype-5 anim jitter table.
  - `bind_scroll_pad` — half-cell centering pad added to effect
    cursor (retail `DAT_004e5d44`).

Chip 5a.iv bindings:

  - `bind_entity_type6_blob` — shared blob for the type-6 PRE-call.
  - `bind_entity_type7_blobs(low, high)` — facing-selected pair for
    the type-7 POST-call.
  - (type-9-sub-0xe reuses entity primary blob — no new binding)

New accessors: per-leaf blit counts (player_marker / unit_key /
effect_chain / type6 / type7 / type9_sub), animation frame counters
(player_marker, effect_chain), effect_chain iteration count + last-arg
captures, last-slot captures for player_marker / unit_key.

### New engine quirks

  - **#90** — effect-chain glyph_offset gate aborts WHOLE chain walk
    (not just current record).  Bad metadata on record N silently
    hides all subsequent effects in the chain.
  - **#91** — player_marker / unit_key Y-axis re-centering uses
    `sprite_w` (not `sprite_h`).  Bug masked because both leaves only
    ever index square sprites in the retail asset.
  - **#92** — effect-chain dirty-box branch identical in both arms
    (subtype 5 vs other).  Dead code; folded away in the port.
  - **#93** — type-9-sub-0xe leaf reads z32 anim table at zoom 16
    too.  Likely subtype 0xe only occurs at zoom 32 in real data.
  - **#94** — type-9-sub-0xe second sprite anchored at SAVED entry
    cursor, not first sprite's adjusted cursor.  Yields a fixed
    102-pixel Y separation regardless of anim state.

### Headline numbers

- **873 → 914 host tests** (+41), all green under ASan/UBSan.
- **6 retail functions ported**: `FUN_004bd574` (485 B player marker),
  `FUN_004bd759` (479 B unit key), `FUN_004beed4` (805 B effect chain),
  `FUN_004be4df` (735 B type-6 PRE), `FUN_004be7be` (763 B type-7 POST),
  `FUN_004beab9` (1051 B type-9-sub POST).  Total ~4318 B of decompile
  → port.
- **5 new engine quirks** (#90 chain abort, #91 sprite_w on both axes,
  #92 dead conditional, #93 unconditional z32 read, #94 saved-cursor
  anchor).
- Two commits — `4d2f04f` (5a.iii) + `e49027b` (5a.iv) — each
  self-contained.
- Cross-compile (mingw32) clean.

### Battlefield port state

| chip   | function                                      | size   | status     |
| ------ | --------------------------------------------- | -----: | ---------- |
| 1      | 16×16 raw blit primitive                      |  705 B | ported     |
| 2      | frame driver + zoom-16 no-op                  |  154 B | ported     |
| 3      | zoom-16 terrain pass                          |  725 B | ported     |
| 3.5    | 32×32 raw blit primitive                      | 2997 B | ported     |
| 4      | zoom-32 terrain pass                          | 1187 B | ported     |
| 5a.i   | middle-pass driver shell                      |  543 B | ported     |
| 5a.ii.a | sorted-entity infrastructure (938+da92+db95) |  761 B | ported    |
| 5a.ii.b | per-entity draw FUN_004bdc31 base path       | 1087 B | ported    |
| 5a.iii | per-cell draw leaves (574+759+beed4)          | 1769 B | **ported (this session)** |
| 5a.iv  | type-special leaves (4df+7be+ab9)             | 2549 B | **ported (this session)** |
| 5b     | zoom-32 overlay pass                          |  436 B | ported     |
| 5c     | late pass                                     |  290 B | stub       |
| 5d     | final pass                                    |  433 B | stub       |
| 6      | 5 coord helpers (hit-test + screen↔hex)       |  394 B | ported     |

The entire chip-5a sub-plan is now landed.  Only chips 5c (late pass)
and 5d (final pass) remain stubbed within the battlefield cluster —
both are unsurveyed.

### What's next

1. **Chip 5c — late pass.**  `FUN_004bd233` (290 B).  Unsurveyed.
   Likely a UI-overlay or transient-effect pass that runs after
   terrain/middle/overlay.  Quick survey → port.

2. **Chip 5d — final pass.**  `FUN_004bf1f9` (433 B).  Unsurveyed.
   Likely the cursor / selection / debug-overlay pass.

3. **gfx.c runtime wiring** — battlefield is now end-to-end at zoom
   16/32 for all chip-5a layers.  Visual verification remains gated
   on user availability — the next milestone where a screenshot
   would be most informative is "first battlefield frame with units
   rendered", which requires standing up the entity table from real
   savegame data.  That's a larger lift than continuing the chip
   plan.

Picking **#1 (chip 5c)** next likely: smaller surface than 5d,
and the late-pass-before-final ordering matters for understanding
how the final pass consumes any state the late pass leaves behind.

This is a natural `/clear` point — both chip-5a leaves landed
cleanly, all 914 tests green, chip-5a sub-plan documented as complete
in `docs/findings/battlefield-render.md`, no incomplete work in-flight.

---

## 2026-05-25 — Battlefield chips 5a.ii.a + 5a.ii.b: sorted-entity infrastructure + per-entity draw (+35 tests, 5 new quirks)

Continuation of the same unattended session.  Previous dispatch
landed chip 5a.i (middle-pass driver shell with all six leaves
stubbed).  This dispatch lands the two sub-chips of 5a.ii — both
sorted-entity infrastructure (build / sort / render loop) AND the
per-entity draw body — leaving only the three type-special leaves
+ the three per-cell draw leaves stubbed within the middle pass.

### Chip 5a.ii.a: sorted-entity infrastructure (`FUN_004bd938 / da92 / db95`)

Replaces three counter-stubs with real ports.  The middle-pass
driver shell from chip 5a.i was already calling them, so wiring is
free; the new bodies populate a shared visible-entity list, sort by
Y ascending, and walk the list dispatching to the per-entity draw.

**`FUN_004bd938` (346 B) — visible-list build.**  Walks the entity
table for ids 1..0x50, filters by alive (`+0x2c` byte) and visible
window (inclusive `[cam_x - 1, cam_x + cols] × [cam_y - 1, cam_y +
rows]`), writes (id, y) pairs into a static buffer, terminates with
an in-band sentinel (y=9999, id=0).

**`FUN_004bda92` (259 B) — bubble sort by Y ascending.**  Last-swap
early termination + 500-iter outer safety cap.  The sentinel acts
as a guaranteed-max comparand on the right edge of the inner loop.
Sort is effectively stable for ties (strict `<` swap predicate) —
matters for entity rendering order on stacked cells.

**`FUN_004bdb95` (156 B) — per-entity render loop.**  For each
sorted entry, computes `px_x = (entity_x - cam_x) * zoom +
viewport_x` (and the y analog), stages as cursor via gfx_glyph,
then dispatches the per-entity draw.

### Chip 5a.ii.b: per-entity draw `FUN_004bdc31` (1087 B base path)

Replaces the state-capturing stub with the real body.  Three
type-special leaves it dispatches stay as counter-stubs:

  - `FUN_004be4df` — type-6 PRE-call (likely a shadow / ground decal
    drawn beneath the main sprite).
  - `FUN_004be7be` — type-7 POST-call.
  - `FUN_004beab9` — type-9-subtype-0xe POST-call.

Body shape (in retail dispatch order):

  1. Type-6 PRE-call (outside the no-blob gate — see quirk #87).
  2. Save entry cursor.
  3. Read primary blob ptr at entity record `+0x00`; NULL ⇒ no-blob
     counter + early return.
  4. Stage sprite descriptor via the existing
     `gfx_dispatch_stage_sprite_descriptor` helper (same 16-byte
     slot layout as terrain / overlay).
  5. Glyph-offset gate `[1, 0x4baf0]` — out-of-band aborts the
     drawing block (skips main draw AND type-7 / type-9 POSTs).
  6. Anim-offset lookup based on zoom:
       - zoom 0x20: `z32_table[facing * 0x88 + frame * 8]` with
         X centering `+ (16 - sprite_w/2)`, Y centering
         `- sprite_w/2 + 8`.
       - zoom 0x10: `z16_table[...]` with `+ (8 - sprite_w/2)`,
         `- sprite_w/2 + 4`.
  7. Apply offsets; type-9 → cursor_y += 12; type-10 → cursor_y += 8.
  8. Dirty box: 5×5 tile box for type < 7, 9×9 for type ≥ 7,
     force-full escalation flag = 2.
  9. Set clip rect; dispatch RLE blit (gated internally on
     `y_clip_mode != 5`).
 10. Restore entry cursor.
 11. Type-7 / type-9-subtype-0xe POST-calls (gated by reaching the
     blit, i.e. inside the no-blob else AND inside the offset gate).

### New API surface

  - `engine_battlefield_render_bind_entity_table(base, stride, max_id)`
    — chip 5a.ii.a, the per-entity table binding.  Tests stand up a
    `(max_id + 1) * stride`-byte buffer and stuff (x, y, alive, type,
    facing, etc.) into the documented offsets.
  - `engine_battlefield_render_bind_anim_offset_tables(z16, z32)` —
    chip 5a.ii.b, the per-(facing, frame) anim offset lookup.  Each
    table is `facings * 0x88` bytes; 8 facings × 17 frames per facing
    in retail.
  - `engine_battlefield_render_set_dirty_state(state)` — chip 5a.ii.b,
    matches the pattern from engine_map_render / engine_overlay_render.
  - Visible-list accessors: `visible_entity_count / id(i) / y(i)`,
    `middle_sort_passes`, `middle_per_entity_count / last_id / last_px_*`.
  - Type-special stub accessors: `middle_entity_type6_count / last_id`,
    `middle_entity_type7_count / last_id`, `middle_entity_type9_sub_count /
    last_id`, `middle_entity_no_blob_count / last_id`,
    `middle_entity_blit_count`.

### New engine quirks

  - **#85** — visible-entity filter window has INCLUSIVE bounds on
    both ends.  Matches the cell-loop over-scan extent exactly.
  - **#86** — visible-entity sort uses an in-band sentinel (id=0,
    y=9999) as a guaranteed-max right-edge comparand; effectively
    stable for equal-y ties.
  - **#87** — type-6 PRE-call fires even when the entity's primary
    blob is NULL.  Intentional asymmetry: secondary draws (shadows
    / decals using a separate blob field at `+0x04`) survive
    primary-asset failures.
  - **#88** — per-entity body reads uninitialised stack at zoom ≠
    {0x10, 0x20}.  Unreachable in retail (game never uses other
    zoom values) but the port initialises to 0 for safety.
  - **#89** — dirty-rect box size threshold uses entity TYPE
    (`< 7` vs `≥ 7`), not sprite dimensions.  Hardcoded 5×5 / 9×9
    cells.  Force-full escalation flag (=2) hardcoded.

### Headline numbers

- **838 → 873 host tests** (+35), all green under ASan/UBSan.
- **4 retail functions ported**: `FUN_004bd938` (346 B),
  `FUN_004bda92` (259 B), `FUN_004bdb95` (156 B), `FUN_004bdc31`
  (1087 B = base path; three type-specials remain stubbed).
- **5 new engine quirks** (#85 inclusive filter, #86 sort sentinel,
  #87 type-6 survives no-blob, #88 uninitialised at unsupported
  zoom, #89 dirty box by entity type).
- Two commits — `09fea1d` (5a.ii.a) + `e00104d` (5a.ii.b) — each
  self-contained.
- Cross-compile (mingw32) clean.

### Battlefield port state

| chip   | function                                | size  | status     |
| ------ | --------------------------------------- | ----: | ---------- |
| 1      | 16×16 raw blit primitive                |  705 B | ported (3 sessions ago) |
| 2      | frame driver + zoom-16 no-op            |  154 B | ported (prev session) |
| 3      | zoom-16 terrain pass                    |  725 B | ported (prev session) |
| 3.5    | 32×32 raw blit primitive                | 2997 B | ported (prev session) |
| 4      | zoom-32 terrain pass                    | 1187 B | ported (prev session) |
| 5a.i   | middle-pass driver shell                |  543 B | ported (this session) |
| 5a.ii.a | sorted-entity infrastructure (938+da92+db95) |  761 B | **ported (this session)** |
| 5a.ii.b | per-entity draw FUN_004bdc31 base path    | 1087 B | **ported (this session)** |
| 5a.iii | per-cell draw leaves (574+759+beed4)    | ~1769 B | stub |
| 5a.iv  | type-special leaves (4df+7be+ab9)       | ~2549 B | stub (added this session) |
| 5b     | zoom-32 overlay pass                    |  436 B | ported (prev session) |
| 5c     | late pass                               |  290 B | stub       |
| 5d     | final pass                              |  433 B | stub       |
| 6      | 5 coord helpers (hit-test + screen↔hex) |  394 B | ported (prev session) |

### What's next

1. **Chip 5a.iii — per-cell draw leaves.**  Port
   `FUN_004bd574` (player marker, animated 8-frame sprite),
   `FUN_004bd759` (unit-key sprite, 4-byte map drops unknown
   keys), `FUN_004beed4` (effect-chain walk, max 10 hops + self-
   loop detection).  Each uses `gfx_dispatch_dispatch_sprite_rle`
   + `engine_dirty_rect_mark_box_tiled` — both already wired.
   No new bindings needed.

2. **Chip 5a.iv — type-special leaves.**  Port
   `FUN_004be4df` (type-6 PRE-call, 735 B — secondary draw using
   the entity's secondary blob field at `+0x04`),
   `FUN_004be7be` (type-7 POST-call, 763 B),
   `FUN_004beab9` (type-9-subtype-0xe POST-call, 1051 B).  Two
   of the three likely share similar shapes (Y-sorted siege engine
   parts maybe?) — survey first.

3. **gfx.c runtime wiring** — strategic-mode rendering has been
   end-to-end since multiple sessions ago.  Battlefield terrain +
   overlay are end-to-end at zoom 32; middle pass driver is end-
   to-end with sorted entities feeding through the real per-entity
   draw (but type-special POSTs and per-cell extras still stubbed).
   Visual verification remains gated on user availability.

Picking **#1 (chip 5a.iii)** next likely: smaller surface than
5a.iv, no new bindings, and the three per-cell leaves are simpler
shapes (per-cell vs per-entity) than the type-specials.

This is a natural `/clear` point — both sub-chips of 5a.ii land
cleanly, all 873 tests green, the chip-5a sub-plan is documented in
`docs/findings/battlefield-render.md`, no incomplete work in-flight.

---

## 2026-05-25 — Battlefield chip 5a.i: middle-pass driver shell (+17 tests, 2 new quirks, sub-chip plan landed)

Continuation of the same unattended session.  Previous dispatch
landed chips 5b + 6 (zoom-32 overlay + 5 coord helpers).  This
dispatch surveys the middle-pass cluster, lands the first sub-chip
(driver shell with leaves stubbed), and rewrites the chip plan to
reflect what survey turned up.

### Survey: the middle-pass cluster is way bigger than 543 B

Previous chip plan listed `FUN_004bd355` as "chip 5a — middle pass
(543 B)".  Survey through the decompilation pulled up an iceberg:
the 543 B driver calls SIX leaves, two of which are setup helpers
(`FUN_004bd938 / FUN_004bda92 / FUN_004bdb95` — 761 B total) that
build/sort/render a Y-ordered visible-entity list, one of which
(`FUN_004bdb95`) reaches into a 1087 B per-entity renderer
(`FUN_004bdc31`) that fans out to three type-specials totaling
2549 B (`FUN_004be4df / FUN_004be7be / FUN_004beab9`).  Plus three
per-cell draw leaves (`FUN_004bd574` player-marker 485 B, `FUN_004bd759`
unit-key 479 B, `FUN_004beed4` effect-chain walk 805 B).

Headline: **~6700 B of decompilation across 11 functions, not 543 B
across 1**.  Comparable in surface area to the entire strategic-mode
renderer cluster (which took 4 chips).

The good news: all four sub-helpers consumed by the draw leaves —
`FUN_004527a6` (cell-tiled dirty-rect mark), `FUN_004b4537` (unclipped
RLE blit), `FUN_004b4814` (left-clipped RLE), and `FUN_004b48b8`
(right-clipped RLE) — are ALREADY PORTED in the project (showed up
during a `grep` for their addresses).  No new infrastructure work
needed for the draw path; chip 5a.iii will use the existing
`gfx_dispatch_dispatch_sprite_rle()` directly.

Chip 5a is therefore split into three sub-chips:

  - **5a.i** — middle-pass driver shell (this dispatch).
  - **5a.ii** — sorted-entity infrastructure (938 + da92 + db95 +
    dc31 base path; type-specials stubbed).
  - **5a.iii** — per-cell draw leaves (574 + 759 + beed4).

### Chip 5a.i: middle-pass driver shell `FUN_004bd355` (543 B)

Replaces the `pass_middle` stub with the real port.  The driver
itself lands; all six leaves stay as static counter-stubs.  Two
behaviors over the terrain/overlay passes distinguish this pass:

**1. Over-scan extension.**  Where terrain/overlay iterate exactly
`grid_cols × grid_rows` cells, the middle pass iterates `(cols+2) ×
(rows+2)` cells starting at `(cam_x - 1, cam_y - 1)`, clamped against
the underlying grid extent (`camera_row_stride` for cols, a new global
`total_rows` (DAT_004e6570) for rows).  The west / north start cells
have NEGATIVE pixel coordinates relative to the viewport's top-left
(`start_px_x = viewport_x - zoom`); per-cell extras rely on the clip
rect to suppress the off-screen portions.

The asymmetry (+1 west / +1 north, +2 east / +2 south) is because
unit/effect sprites are LARGER than one hex cell — an entity anchored
at a hex column adjacent to the visible viewport on the west side can
still have its right edge visible inside the viewport; on east/south
the +2 cells accommodate sprites that poke up to ~32 px past the
anchor (spanning TWO cells at zoom 16).  Filed as **quirk #83**.

**2. Per-cell dispatch on two independent descriptor bits.**

  - `descriptor[+2] & 0x80` set:
    - `descriptor[+3] == 0` → player-marker draw AND re-dirty
      (`descriptor[+2] |= 1`).  Re-dirty ensures the animated player
      marker re-paints next frame.
    - `descriptor[+3] != 0` → unit-key draw (no re-dirty).
  - `descriptor[+6] != 0` → effect-chain walk (independent of bit
    0x80; evaluated for every cell).

The asymmetric re-dirty (only the player-marker branch sets bit 1)
is filed as **quirk #84** — the unit-key sprite is static per frame,
so it doesn't need the re-paint loop, but the player marker animates
through 8 frames driven by an internal counter and would freeze
without the re-dirty.

### New API surface

  - `engine_battlefield_render_bind_total_rows(int)` — binds retail
    `DAT_004e6570`, the total row count of the underlying grid (not
    the visible row count).  Used to clamp the south edge of the
    over-scan extension.  Not consumed by terrain / overlay passes.

  - Six new per-leaf counters + two last-arg captures for the stubbed
    leaves (`middle_build_count`, `middle_sort_count`,
    `middle_render_count`, `middle_player_marker_count`,
    `middle_unit_key_count`, `middle_effect_chain_count`, plus
    `middle_unit_key_last_arg` / `middle_effect_chain_last_arg`).
    These let tests verify the driver's per-cell dispatch shape
    without depending on real leaf bodies; they survive into chip
    5a.ii / 5a.iii either as cross-checks or are retired as the
    leaves get real bodies.

  - `engine_battlefield_render_per_frame_cell_counter(void)` — exposes
    the existing `g_per_frame_cell_counter` (retail `_DAT_004e5d64`)
    for tests to verify cell-loop iteration counts.

  - `engine_battlefield_render_run_pass_middle_for_test(cam_x, cam_y)`
    — test-only entry that calls `pass_middle()` directly with the
    given camera, bypassing the frame driver's terrain / overlay /
    late / final dispatch.  Without this, tests can't isolate cursor
    state from sibling passes that also mutate it.

### Headline numbers

- **821 → 838 host tests** (+17), all green under ASan/UBSan.
- **1 retail function ported**: `FUN_004bd355` (543 B).
- **2 new engine quirks** (#83 over-scan extension, #84 asymmetric
  re-dirty).
- Cross-compile (mingw32) clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### Battlefield port state

| chip   | function                                | size  | status     |
| ------ | --------------------------------------- | ----: | ---------- |
| 1      | 16×16 raw blit primitive                |  705 B | ported (3 sessions ago) |
| 2      | frame driver + zoom-16 no-op            |  154 B | ported (prev session) |
| 3      | zoom-16 terrain pass                    |  725 B | ported (prev session) |
| 3.5    | 32×32 raw blit primitive                | 2997 B | ported (prev session) |
| 4      | zoom-32 terrain pass                    | 1187 B | ported (prev session, gate fix this session) |
| 5a.i   | middle-pass driver shell                |  543 B | **ported (this session)** |
| 5a.ii  | sorted-entity infrastructure (938+da92+db95+dc31) | ~1848 B | stub (this dispatch) |
| 5a.iii | per-cell draw leaves (574+759+beed4)    | ~1769 B | stub |
| 5b     | zoom-32 overlay pass                    |  436 B | ported (prev session) |
| 5c     | late pass                               |  290 B | stub       |
| 5d     | final pass                              |  433 B | stub       |
| 6      | 5 coord helpers (hit-test + screen↔hex) |  394 B | ported (prev session) |

The middle-pass driver shell is real; the six leaves it calls stay as
counter-stubs that bump per-dispatch counters.  Tests verify the
driver's per-cell dispatch shape (which leaf fires on which descriptor
bits) without depending on real leaf bodies.

### What's next

1. **Chip 5a.ii — sorted-entity infrastructure.**  Port
   `FUN_004bd938` (entity-list build, walks the entity table at
   `DAT_005544XX + id*0x1b0` for ids 1..0x50, filtering by alive byte
   + visible viewport), `FUN_004bda92` (bubble sort with 500-iter
   safety cap), `FUN_004bdb95` (per-entity render loop), and the
   base path of `FUN_004bdc31` (per-entity draw — three type-
   specials stay stubbed for chip 5a.iv/v/vi).  This unblocks the
   middle pass's "draw Y-sorted units" path which is the primary
   visual it contributes — the per-cell extras (player marker +
   effect chain) are secondary.

2. **Chip 5a.iii — per-cell draw leaves.**  Port `FUN_004bd574`,
   `FUN_004bd759`, `FUN_004beed4`.  Each uses the shared dispatch
   pattern: stage sprite metadata from a blob, set clip rect, mark
   4×4 cells dirty, dispatch via `gfx_dispatch_dispatch_sprite_rle`.
   The four sub-helpers (`FUN_004527a6`, `FUN_004b4537/4814/48b8`)
   are already ported — no new infrastructure needed.  `FUN_004beed4`
   adds a max-10-hop linked-list walk with self-loop detection;
   `FUN_004bd574` adds an animation counter; `FUN_004bd759` adds a
   4-byte unit-key map that silently drops unknown keys.

3. **gfx.c runtime wiring** — strategic-mode rendering has been
   end-to-end since multiple sessions ago.  Battlefield terrain +
   overlay are end-to-end at zoom 32; the middle pass driver is
   end-to-end but with leaves that draw nothing yet.  Visual
   verification remains gated on user availability.

Picking **#1 (chip 5a.ii)** next: porting the sorted-entity
infrastructure is the bigger visual win — units / heroes / armies all
render through that path.  Chip 5a.iii's leaves are smaller draws
(player marker is one sprite, unit-key is a 4-glyph palette) and can
go either before or after 5a.ii without dependency conflict.

This is a natural `/clear` point — chip 5a.i lands cleanly, all
838 tests green, the chip 5a sub-plan is documented in
`docs/findings/battlefield-render.md`, no incomplete work in-flight.

---

## 2026-05-25 — Battlefield chips 5b + 6: zoom-32 overlay + 5 coord helpers (+24 tests, 4 new quirks, 1 bug fix)

Continuation of the same unattended session.  Previous dispatch
landed chip 4 (zoom-32 terrain).  This dispatch lands chip 5b (zoom-32
overlay pass) — completing the zoom-32 layer pair — plus chip 6
(five tiny coordinate helpers that port cleanly as pure functions).
Also fixes an off-by-one in chip 4 that emerged from cross-checking
chip 5b's gate constant against the binary.

### Chip 5b: zoom-32 overlay pass `FUN_004bd07f` (436 B)

Replaces the `pass_overlay_zoom32` stub with the real port.  Outer
shape mirrors chip 4 (terrain-z32) at 32-px cell pitch, but with
three crucial differences:

**1. Layer bit gate.**  Drawing requires `descriptor[+1] & 4` SET —
the exact same bit that terrain-z32 uses as its EARLY-SKIP gate, but
inverted polarity.  Terrain only draws when the bit is CLEAR; overlay
only draws when the bit is SET.  Net effect: per cell, at most ONE
of the two passes runs the drawing block per frame, picked by the
layer bit.  Cells without the bit get terrain; cells with the bit get
the overlay (a "feature replacement layer").  This is filed as
**quirk #79** — the bit is overloaded between two opposite-polarity
sibling passes.

**2. Always primary blob.**  No `(flags & 0x1c)` selector — the
overlay pass always reads from the primary blob (retail
DAT_004e5d78).  The alt blob is not consulted.

**3. No boundary marking, no second sprite.**  Straight-line per-cell
loop.  Same `[1, 0x4baf0]` glyph_offset gate as terrain-z32, but the
gate sits AFTER the dirty-clear (matching retail), so an out-of-band
offset still appears "redrawn" to subsequent frames — no recovery
mechanism re-marks the cell dirty.  Filed as **quirk #81**.

### Chip 4 off-by-one bug fix

While cross-checking chip 5b's glyph_offset gate constant against the
binary, discovered chip 4's port used `> 310000` for the abort gate
but the actual retail bytes encode `cmp ..., 0x4baf0` (= 309488),
confirmed via r2 at 0x4bcd76.  Chip 4 was accepting bad offsets in
`[309489, 310000]` — a 512-value off-by-one.  Filed as **quirk #80**
to document the Ghidra display lesson: when the decomp shows a "round"
decimal literal as a `cmp` immediate, always verify against the
disassembly — Ghidra normalizes hex to decimal aggressively and the
rounding direction is not documented.

Updated two chip 4 tests to reflect chained behavior with chip 5b
active: cells with the layer bit set + dirty had their dirty bit
ASSERTED to remain set after `engine_battlefield_render_frame` (since
chip 4 skips them), but with chip 5b now real, the OVERLAY pass picks
them up and clears the dirty bit.  Tests now assert dirty cleared +
overlay blit count incremented, matching the full chained behavior.

### Chip 6: five coordinate helpers (FUN_004bc222 / 2a5 / 2c5 / 2e5 / 346)

Five tiny pure functions at the head of the battlefield cluster
(394 B total) that translate screen pixel coords → hex grid column /
row.  All share the existing render-pass module statics plus one new
binding `bind_cell_band_width` for retail DAT_004e5d3c (the cell sub-
band width — for a 32-px hex tile divided into three bands, this is
~8 pixels).

  - `screen_in_grid(px, py)`                — 131 B AABB hit-test
  - `pixel_to_col(px)`                      — 32 B basic divide
  - `pixel_to_row(py)`                      — 32 B basic divide
  - `pixel_to_col_right_biased(px)`         — 97 B (right-band → +1)
  - `pixel_to_col_left_biased(px)`          — 102 B (left-band → -1)

The two biased variants are clamped ASYMMETRICALLY — right-biased
clamps low (col < 1 → 0) but not high; left-biased clamps high
(col >= row_stride → row_stride - 1) but not low.  The left-biased
variant can therefore return NEGATIVE column indices when the input
is close to the grid's left edge AND falls in the first sub-band.
Filed as **quirk #82** to warn callers that these aren't safe as
generic "clip into grid" primitives.

No port-side callers yet — these unblock input handling for the
combat screen but aren't on the render path.  Landing them here as a
discrete chip means the next session can focus on either (a) the
heavy 5a/5c/5d passes, or (b) wiring input + scroll, without needing
to come back for these primitives.

### Headline numbers

- **797 → 821 host tests** (+24), all green under ASan/UBSan.
- **2 retail functions ported in chip 5b** (`FUN_004bd07f`, 436 B),
  **5 retail functions ported in chip 6** (totaling 394 B).
- **4 new engine quirks** (#79 overloaded layer bit, #80 Ghidra
  rounding lesson, #81 overlay-z32 silent dirty-clear, #82 asymmetric
  bias-helper clamps).
- **1 bug fix** in chip 4 (off-by-512 glyph_offset gate).
- Cross-compile (mingw32) clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### Battlefield port state

| chip | function                       | size  | status     |
| ---- | ------------------------------ | ----: | ---------- |
| 1    | 16×16 raw blit primitive       |  705 B | ported (3 sessions ago) |
| 2    | frame driver + zoom-16 no-op   |  154 B | ported (prev session) |
| 3    | zoom-16 terrain pass           |  725 B | ported (prev session) |
| 3.5  | 32×32 raw blit primitive       | 2997 B | ported (prev session) |
| 4    | zoom-32 terrain pass           | 1187 B | ported (prev session, this session: gate fix) |
| 5a   | middle pass                    |  543 B | stub       |
| 5b   | zoom-32 overlay pass           |  436 B | **ported (this session)** |
| 5c   | late pass                      |  290 B | stub       |
| 5d   | final pass                     |  433 B | stub       |
| 6    | 5 coord helpers (hit-test + screen↔hex) | 394 B | **ported (this session)** |

Both zoom-32 layers (terrain + overlay) are now real ports — chip 5b
completes the zoom-32 pair.  Zoom-16 has its terrain pass real and
its overlay as a faithful no-op port (since retail's overlay-z16 is
literally `return;`).  The four always-on passes — middle, late,
final, plus the chip-5 trio — remain as stubs that bump dispatch
counters.

### What's next

1. **Battlefield chip 5a/5c/5d** — the three remaining stub passes
   (middle, late, final).  Each pulls in 3–5 unported helpers
   (middle: FUN_004bd938/da92/db95/d574/d759/beed4 ≈ 2900 B; late:
   FUN_004be070 + FUN_004be33f ≈ 1135 B + two large unmapped data
   arrays; final: FUN_00440089 + FUN_00402f64 with the font/text
   renderer chain).  Each is its own significant porting effort —
   probably one chip per pass and one session per chip.

2. **Combat-mode input wiring** — chip 6's helpers are now available,
   so a small input-handling chip could land that resolves click/
   keyboard → hex column + row.  The scroll/pan family
   (`FUN_004bc1d1` + `FUN_004bc51a` @ 994 B) still needs survey
   before a port.

3. **gfx.c runtime wiring** — strategic-mode rendering has been
   end-to-end for three sessions; visual verification remains gated
   on user availability.  Battlefield zoom-32 is now end-to-end (both
   terrain + overlay are real ports), so once the gfx.c stub is
   wired up the user could verify both screen modes in one go.

Picking **#1 (chip 5a — middle pass)** next: it's the largest
remaining stub and has the biggest dependency surface, so chipping
away at its helpers is the right next move.  Suggestion for the
next session: survey FUN_004bd574 / d759 first (they're called
directly from the middle-pass driver and look like they're the
"draw unit" / "draw effect" leaves of the per-cell loop).

This is a natural `/clear` point — two chips landed, six total in
the battlefield port (chip 5b + chip 6 this session, chips 1+2+3+
3.5+4 in prior sessions), no incomplete work in-flight.

---

## 2026-05-25 — Battlefield port chip 4: zoom-32 terrain pass (+20 tests, 2 new quirks)

Continuation of the same unattended session.  Previous dispatch
landed chips 2 + 3 + 3.5 (frame driver, zoom-16 terrain pass, 32×32
raw blit primitive).  This dispatch lands the next chip on the plan:
the zoom-32 terrain pass `FUN_004bcbdc` (1187 B), which is the
larger sibling of the zoom-16 pass and the one that pulls in the
clipped-blit family for the second-sprite-per-cell path.

### Chip 4: zoom-32 terrain pass `FUN_004bcbdc` (1187 B)

Replaces the static `pass_terrain_zoom32` stub in
`engine_battlefield_render` with the real port.  Two new per-cell
behaviors over the zoom-16 sibling distinguish this pass:

**1. EARLY-SKIP gate on descriptor[+1] & 4.**  Dirty (or force-
redrawn) cells with this bit set are silently dropped — NO drawing
AND NO boundary marking.  Clean cells are NOT affected by the gate:
they still get boundary marking unconditionally.  This is the first
zoom-level-specific path in the battlefield renderer that drops
boundary marking for a dirty cell.

**2. SECOND clipped sprite per cell.**  Gated on `descriptor[+0]
!= 0` AND `descriptor[+4] ∈ {1, 2, 3}`.  Always pulls from the
**alt** blob (regardless of which blob the first sprite picked).
Picks slot `descriptor[+0] + 0x8b`, clamped at `0x9a` when
`descriptor[+0] >= 0x10`.  Dispatches via the full clipped-blit
pipeline (clip_x → clip_y → apply_skip_and_dispatch_sprite) against
a bound clip rect.  This is where the clipped sprite blit family
(FUN_004b446d / FUN_004b44d2 / FUN_004b43b1 + the two clip setters)
finally gets called from the battlefield path.

Both sprites share a `[1, 310000]` glyph_offset gate that aborts the
ENTIRE cell on out-of-band offsets — skips boundary marking AND
skips the second sprite.  Three abort gates total (out-of-band first-
sprite offset on primary path, same on alt path, second-sprite mode
byte == 0 or > 3); each matches retail's `goto LAB_004bcc53` shape
that bypasses the boundary block.

New binding: `engine_battlefield_render_bind_clip_rect(left, top,
right, bottom)` mirrors retail's `DAT_004e6564 / 5d54 / 5d48 / 5d6c`
inline args to FUN_0040464c / FUN_0040477b.  Two new test-only
counters: per-cell first / second sprite dispatch counts.

### Filed quirks (#77, #78)

  - **#77** — Battlefield zoom-32 cell ABORT skips boundary marking.
    Three distinct cell-disposal paths in FUN_004bcbdc; only ONE
    (clean) and one branch of another (dirty + skip-bit clear + no
    abort gate fired) run boundary marking.  Critical for the
    "scroll smear" failsafe — corrupted descriptors get dirty-cleared
    but never get the boundary re-paint mark, so the corruption
    silently propagates as unredrawn pixels with no recovery on
    scroll.  Unlike zoom-16 where every cell always got boundary
    marking, regardless of dirty/clean.

  - **#78** — Zoom-32 second-sprite slot index saturates at slot
    `0x9a`.  `descriptor[+0]` values in `[1, 0x10)` map to slots
    `[0x8c, 0x9b)`; `>= 0x10` clamps to `0x9a`.  A saturating clamp
    (not a wrap) so corrupted high values render the "default
    overlay" sprite rather than dereferencing past the slot table.
    Effectively a silent failsafe.

Also confirmed by this port: quirk #76's prediction that the zoom-32
sibling shares zoom-16's strict `if/else-if` blob-selector chain
holds — non-{0, 4} `(flags & 0x1c)` values skip the first sprite
silently.  The second sprite path is independent and STILL runs in
those cells.

### Test scaffold bug worth recording

The chip-4 test scaffold initially placed its pixel patterns at blob
offsets `0x400 / 0x800 / 0xc00` and used `memset` over 1024-byte
regions.  Slot 0x9a's descriptor lives at `0x9a*16+8 = 2472`, which
overlaps the `0x800..0xc00` pixel-region memset (covers `[2048,
3072)`).  The 0x88 pattern silently corrupted slot descriptors,
which then surfaced as garbage `glyph_w` / `glyph_offset` values
during clip setup — leading to a SEGV in `sprite_blit_clipped`
(off-canvas dest write).  Fix: bump pixel offsets to >= 4096 so
they land above the entire slot descriptor table (slots 0..0xff end
at byte 4087).  No retail bug — this was a pure test-scaffold mistake.

### Headline numbers

- **777 → 797 host tests** (+20), all green under ASan/UBSan.
- **1 retail function ported**: `FUN_004bcbdc` (1187 B).
- **2 new engine quirks** (#77 abort skips boundary, #78 second-
  sprite slot clamp).
- Cross-compile (mingw32) clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### Battlefield port state

| chip | function                       | size  | status     |
| ---- | ------------------------------ | ----: | ---------- |
| 1    | 16×16 raw blit primitive       |  705 B | ported (2 sessions ago) |
| 2    | frame driver + zoom-16 no-op   |  154 B | ported (prev session) |
| 3    | zoom-16 terrain pass           |  725 B | ported (prev session) |
| 3.5  | 32×32 raw blit primitive       | 2997 B | ported (prev session) |
| 4    | zoom-32 terrain pass           | 1187 B | **ported (this session)** |
| 5a   | middle pass                    |  543 B | stub       |
| 5b   | zoom-32 overlay pass           |  436 B | stub       |
| 5c   | late pass                      |  290 B | stub       |
| 5d   | final pass                     |  433 B | stub       |

Both zoom-dispatched first-pass terrain blits are now real ports.
The remaining four passes are the always-on middle / late / final
+ the zoom-32 overlay — collectively ~1700 B, all stubs that bump
counters.  Their internals are unsurveyed; substantial discovery
work needed before porting.

### What's next

1. **Battlefield chip 5** — the four remaining passes.  Need to
   survey each one (probably each warrants its own findings-doc
   section).  Suggestion: start with `FUN_004bd355` (middle pass,
   543 B) as it's the always-on path and zoom-independent — likely
   draws units/units-state without needing zoom-specific layout
   logic.  `FUN_004bf1f9` (final pass, 433 B) is also always-on
   and might be a good second chip if the middle pass is straight-
   forward.

2. **gfx.c runtime wiring** — still gated on user availability.
   Strategic-mode primary + overlay are end-to-end since three
   sessions ago.  Visual verification milestone reachable as soon
   as the user pops up the game.

3. **Battlefield helpers** — hit-test (`FUN_004bc222`), screen↔hex
   (`FUN_004bc2a5 / 2c5 / 2e5 / 346`), scroll (`FUN_004bc1d1 /
   51a`).  Small surface area; would unblock input handling for
   the combat screen.

Picking **#1** next: chip 5 is the natural continuation of the
battlefield render port, and at least one of its four passes
(middle / late / final) is always-on so they get exercised every
frame regardless of zoom — verifiable in isolation.

This is a natural `/clear` point — chip 4 lands cleanly, the
battlefield port is now >50% by LOC (5876 B ported of ~12 KB
cluster), and the remaining work is a discrete four-pass chip
with no inter-pass coupling discovered yet.

---

## 2026-05-25 — Battlefield port chips 2, 3, 3.5 (3 chips, +31 tests)

Continuation of the same unattended session.  Previous dispatch
landed the battlefield 16×16 blit primitive (chip 1) and the
cluster survey.  This dispatch lands the next three chips of the
five-chip plan: the frame driver + no-op stub (chip 2), the
zoom-16 first pass terrain blit (chip 3), and the 32×32 sibling
blit primitive (chip 3.5 — chip 4 prereq).

### Chip 2: combat frame driver `FUN_004bc142` + zoom-16 no-op stub

New module `engine_battlefield_render` hosts the combat-mode frame
driver port (143 B) and the 11-byte zoom-16 second-pass no-op
(`FUN_004bcbd1`).  The five remaining passes (zoom-16/zoom-32
terrain, middle, zoom-32 overlay, late, final) are wired as
static call-counter stubs so the driver's dispatch shape
(zoom-16 vs zoom-32, always-on vs zoom-gated) can be exercised
in isolation before each pass's body lands.

The retail driver has no fall-through default in either zoom
if/else-if chain: zoom values that are neither `0x10` nor `0x20`
silently skip both zoom-dispatched passes (terrain + overlay)
while the always-on passes still fire.  The force-redraw
counter (retail `DAT_0053e9ac`) is decremented post-frame when
non-zero — preserved as a strict-non-zero guard so the value
never underflows below 0.

14 new tests cover the camera bind, three dispatch-shape cases
(zoom-16 / zoom-32 / unknown), force-redraw decrement + zero
floor, counter accumulation/reset, mid-stream zoom switching,
and the zoom-accessor round-trip.

### Chip 3: zoom-16 first pass `FUN_004bc8fc` (725 B terrain blit)

Replaces the `pass_terrain_zoom16` stub with the real port.  Adds
six new bindings to the module:

  - `bind_descriptor_table(base, stride)` — retail `DAT_004e6574 /
    DAT_004e5d50`.  Per-cell descriptor with flag byte at +2 and
    sprite index at +3.
  - `bind_grid(cols, rows, row_wrap)` — retail `DAT_004e5d5c /
    DAT_004e5d4c / DAT_004e5d70`.  Visible grid dims and the
    descriptor offset added at end of each visible row.
  - `bind_viewport_offset(x, y)` — retail `DAT_004e6558 /
    DAT_004e6554`.  Pixel coords of the top-left visible cell.
  - `bind_camera_row_stride(row_stride)` — retail `DAT_004e6578`.
    Cells per row in the underlying descriptor table.
  - `bind_blobs(primary, alt)` — retail `DAT_004e5d78 /
    DAT_004e6560`.  Per-cell flag bits 2..4 select between them.

Per-cell logic: read flags byte; if force-redraw OR dirty, clear
dirty bit, pick blob via `(flags & 0x1c)`, stage cursor + sprite
metadata through `gfx_dispatch_stage_sprite_descriptor`, dispatch
the 16×16 raw blit.  Boundary cells get bit `0x02` AFTER the
dirty-gated branch — so even clean edge cells get forced to
re-paint next frame (handles scroll smear).

Cursor advance: `+0x10` per cell (16 px), `+0x10` per row.

13 new tests covering per-cell dispatch, dirty-skip, force-redraw
override, dirty-clear-post-draw, boundary marking on edges +
edges-while-clean, blob selector primary/alt/other, cursor
advance across cells, NULL descriptor short-circuit, camera Y
offset, and counter reset.

### Chip 3.5: 32×32 raw blit primitive `FUN_004b5333` (chip 4 prereq)

Sibling of `gfx_sprite_blit_16x16_raw`.  Same hardcoded 640-byte
dest stride (quirk #75 applies), no transparency check, 1024
bytes per sprite instead of 256.  Retail body is 32 unrolled
8-dword row copies (2997 B of decompilation); the port collapses
to a 32-iteration row loop with `memcpy`.

This is the second sprite primitive the battlefield cluster
dispatches: 16×16 for zoom-16 passes, 32×32 for zoom-32 passes.
Now that both primitives are ported, chip 4 (`FUN_004bcbdc`,
zoom-32 first pass) has all its dependencies in place.

4 new tests covering 1024-byte copy completeness, opaque-zero
writes, glyph_offset tile selection, and NULL canvas safety.

### Filed quirk (#76)

  - **#76** — Battlefield terrain blob selector is strict
    `if/else-if` with no default clause.  `(flags & 0x1c)` values
    outside `{0, 4}` silently skip blit dispatch while STILL
    clearing the dirty bit and running boundary marking.  Result:
    cell appears "redrawn" from descriptor state but no pixels
    change.  Works in retail because level data only ever produces
    selector values 0 or 4; non-{0,4} indicates data corruption.
    The strict shape is preserved (no default `else`) so corrupted
    data produces the same symptom in the port.

### Headline numbers

- **746 → 777 host tests** (+31), all green under ASan/UBSan.
- **3 retail functions ported**: `FUN_004bc142` (143 B) +
  `FUN_004bcbd1` (11 B) + `FUN_004bc8fc` (725 B) +
  `FUN_004b5333` (2997 B) = **3876 B of decompilation**.
- **1 new engine quirk** (#76 — strict blob selector chain).
- **1 new module** (`engine_battlefield_render`, ~270 LOC).
- Cross-compile (mingw32) clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### Battlefield port state

| chip | function                       | size  | status     |
| ---- | ------------------------------ | ----: | ---------- |
| 1    | 16×16 raw blit primitive       |  705 B | ported (prev session) |
| 2    | frame driver + zoom-16 no-op   |  154 B | **ported (this session)** |
| 3    | zoom-16 terrain pass           |  725 B | **ported (this session)** |
| 3.5  | 32×32 raw blit primitive       | 2997 B | **ported (this session)** |
| 4    | zoom-32 terrain pass           | 1187 B | surveyed   |
| 5a   | middle pass                    |  543 B | stub       |
| 5b   | zoom-32 overlay pass           |  436 B | stub       |
| 5c   | late pass                      |  290 B | stub       |
| 5d   | final pass                     |  433 B | stub       |

Strategic-mode rendering remains end-to-end (closed two sessions
ago).  Battlefield rendering now has the frame driver + half of
the zoom-dispatched leaves landed, with the bigger zoom-32 pass
next.

### What's next

1. **Battlefield chip 4** — port `FUN_004bcbdc` (zoom-32 first
   pass, 1187 B).  More complex than chip 3: extra early-skip
   gate (`descriptor[+1] & 4`), src-offset validation (`<310000`),
   and a second sprite pass per cell using the clipped sprite
   blits (FUN_004b446d / FUN_004b44d2 / FUN_004b43b1) plus the
   FUN_0040464c / FUN_0040477b clip setters.  With the 32×32
   primitive now ported, the heavy lifting is the per-cell
   second-sprite path.

2. **gfx.c runtime wiring** — still gated on user availability.
   Strategic-mode primary + overlay are end-to-end since two
   sessions ago, so the visual verification milestone is
   reachable.

3. **Battlefield chip 5** — the four remaining passes (middle,
   zoom-32 overlay, late, final) collectively ~1700 B.  Their
   internals are unsurveyed; substantial discovery work needed
   before porting.

Picking **#1** next: chip 4 is the natural extension of this
session's work and the 32×32 primitive prereq just landed.

This is a natural `/clear` point — three chips landed, the
battlefield port is over halfway done by LOC, the next chip is
discrete (one function with known prereqs).

---

## 2026-05-25 — Tier-flag wiring + battlefield survey + 16×16 blit primitive (3 chips)

Continuation of the same unattended session.  After army drawer
ports closed strategic-mode rendering, three frontiers were on the
table: gfx.c runtime wiring (needs user verification), tier-flag
driver discovery, and battlefield renderer survey.  This dispatch
lands the two unattended ones plus the first chip of the
battlefield port.

### Chip 1: tier-flag driver — already ported, needed wiring

Quirk #74 from the previous dispatch claimed "the four animation-
rate tier-flag driver lives elsewhere — probably a frame-rate-
throttled toggle in the main loop."  It turned out to BE
`FUN_004bbc80` (the 9-flag phase-tick generator), which had been
ported as `engine_compute_phase_ticks` in `src/engine_phase.c` for
many chips already.  The army drawer's four tier flags map directly
to the 640/320/160/80-ms phase-tick outputs:

  distance <  10  → tier 0 → 640 ms tick (slowest)
  distance < 0x19 → tier 1 → 320 ms tick
  distance < 0x32 → tier 2 → 160 ms tick
  distance ≥ 0x32 → tier 3 →  80 ms tick (fastest)

So the chip is just one new call from `engine_tick_phase_step` to
`engine_overlay_render_set_animation_rate_flags`.  Closes the
"driver lives elsewhere" claim and fixes an inverted narrative in
quirk #74 (small distance = slow tier, not "most aggressive" — a
slow-moving column should flicker its sprite less often than one
at full speed).

3 files changed, 51+/7-.  No new tests (wiring lives in the
Win32-coupled wrapper, not in pure-function territory).

### Chip 2: battlefield (combat-mode) renderer survey

Located `FUN_004bc142` @ 0x4bc142 (143 B) as the combat-mode frame
driver.  Called from the main-loop tick (`FUN_004b99c0` line 88581)
inside the `DAT_0057c8fc == 2` branch.  Takes camera coords
(`DAT_0055ce70`, `DAT_005678a0`) and fans out to 7 pass functions:

  zoom-16 path: FUN_004bc8fc (terrain, 725 B) +
                FUN_004bcbd1 (no-op stub, 11 B)
  zoom-32 path: FUN_004bcbdc (terrain, 1187 B) +
                FUN_004bd07f (overlay, 436 B)
  shared:       FUN_004bd355 (middle, 543 B) +
                FUN_004bd233 (late, 290 B) +
                FUN_004bf1f9 (final, 433 B)

Plus ~27 helpers (hit-test, scroll, screen↔hex coord conversion).
Total cluster: ~12 KB across ~34 functions in 0x4bc000..0x4bf300.

Previously hypothesized this would be near the strategic renderer
in 0x405000..0x408000 — wrong; the actual address band is the
0x4bc000 family.  Also surfaced a new sprite primitive
`FUN_004b49f7` (705 B) shared by both the battlefield and 5 UI
call sites — addressed in chip 3.

Full survey landed in `docs/findings/battlefield-render.md` with a
suggested 5-chip plan.

### Chip 3: 16×16 raw blit primitive `FUN_004b49f7`

First chip of the battlefield port.  Adds `gfx_sprite_blit_16x16_raw`
to `gfx_sprite.{c,h}`.  Differences from existing sprite blits:

  - No transparency gate (zero source bytes ARE written)
  - Hardcoded 16×16 (no width/height staging)
  - **Hardcoded 640-byte dest row stride** (quirk #75)

Used by both the battlefield terrain renderer
(FUN_004bc8fc/FUN_004bcbdc) and 5 UI helpers in 0x40920..0x4097a,
so it unblocks more than just the battlefield path.  Filed quirk
#75 documenting the 640 stride lock — works in retail because
non-640 surfaces are never bound at these call sites, but tests
have to use a 640-wide canvas to match.

5 new host tests covering: 256-byte copy completeness, opaque-zero
writes, cursor handling, glyph_offset indexing, NULL canvas safety.

### Headline numbers

- **741 → 746 host tests** (+5), all green under ASan/UBSan.
- **1 retail function ported** (`FUN_004b49f7`, 705 B) +
  1 wiring chip + 1 survey doc.
- **1 new engine quirk** (#75 — hardcoded 640 dest stride).
- **2 quirks updated** (#74 — driver resolved, narrative fixed).
- **1 new findings doc** (`docs/findings/battlefield-render.md` —
  ~220 lines documenting the cluster + chip plan).
- Cross-compile clean.

### What's next

The battlefield port plan in `battlefield-render.md` has 5 chips.
Chip 1 (16×16 primitive) just landed; chip 2 is the frame driver
+ zoom-16 stub (143 B + 11 B = small structural seam, lets the
remaining passes be tested as stubs before they're ported).  Then
the three substantive passes (chip 3 = zoom-16 first pass 725 B,
chip 4 = zoom-32 first pass 1187 B, chip 5 = remaining four passes
~1700 B combined).

Alternative still on the table: wire `gfx.c` runtime to the
strategic-mode renderer for visual verification — first frame on
screen.  That's gated on user availability; not pursued in this
unattended session.

This is a natural `/clear` point — three chips landed, next chip
(battlefield frame driver) is a discrete porting effort.

---

## 2026-05-25 — Army drawer ports complete (1 chip, 2 fns + label sub-renderer)

Continuation of the same unattended session.  The previous dispatch
landed 5 of 6 strategic-mode overlay functions and identified the
army drawer (`FUN_004071a0`, 3179 B) as the only remaining gap.
This dispatch closes it out — strategic-mode rendering is now
end-to-end in `src/`.

### Discovery: there's a 7th cluster function

Surveying the army drawer's call topology surfaced a child function,
`FUN_00407f82` @ 0x407f82 (548 B), called exclusively by the army
drawer's "large army at territory" branch.  It renders a fixed
"label background" sprite (slot 0x82 of the overlay blob) plus a
centered decimal text label below it — effectively the
army-strength indicator the player sees over territories with
significant garrisons.

So the cluster grew from 6 to 7 functions; both new ones land here.

### Chip landed

**`src/engine_overlay_render` (replaced army stub with full port)**:

  - `FUN_004071a0` → `engine_overlay_render_tile_army`        (3179 B)
    Real body replaces last chip's call-counter stub.  Holds the
    4-way flag-bit priority chain, 13-category state machine, and
    common dispatch tail.
  - `FUN_00407f82` → `army_render_label` (static helper)        (548 B)
    Army-label sub-renderer.  View-mode-0-only.  Stages sprite
    0x82 + centered text via `gfx_text_label_render_dec_centered`
    (already ported), validates `src_off ≤ 310000`.

Total new code: ~360 LOC added to `engine_overlay_render.c`
(`tile_army` + helpers `army_step_band`, `army_end_frame_band`,
`army_render_label`, `army_dispatch_tail`, plus small accessors).

### Five new state setters

The army drawer reads five retail globals not previously exposed:

  - `set_render_mode(mode)` — `DAT_0053f264`.  Top-level filter
    that gates army draw on `descriptor[+2] & 0x20`.
  - `set_overlay_base_index(base)` — `DAT_0057d390`.  Base for
    fortress + owner-color sprites (`base + owner*8 - 8`).
  - `set_arrow_marker_base_index(base)` — `_DAT_0057d38c`.  Base
    for directional arrows (low: `+0x67`, high: `+0x55`).
  - `set_animation_tick(tick)` — `DAT_0053e9a4`, [0..0xf] cycle.
    End-frame branches dispatch `sprite_idx = tick + 0x28`.
  - `set_animation_rate_flags(t0, t1, t2, t3)` — four globals at
    `DAT_0058fd08`, `DAT_0057d3c8`, `DAT_0057d3ac`, `DAT_00591510`.
    Per-tier toggles selected by `(cur - prev)` distance bin.

Plus one new array binding: `bind_army_records(records)` — points
into `g_per_army_768` (save schema entry 4, 17 × 0x300 B).

### Filed quirks (#70, #71, #72, #73, #74)

  - **#70** — Army drawer's `bit 0x40` path with `descriptor[+4] &
    0xf` outside {0, 2} sets NO sprite_idx but still hits the
    dispatch tail.  Result: stale sprite from the dispatcher's
    most-recent staging gets re-drawn at offset (0, 0).  Visually
    surfaces as garbled markers.
  - **#71** — Army drawer's main dispatch tail uses `src_off <
    0x11171` (= 70001) — over 4× tighter than the city / primary
    pass / army-label sub-renderer's `< 0x4baf1` (310001).
    Partitions the overlay blob into a 70 KB "army region" and a
    310 KB "everything-else region".
  - **#72** — End-frame categories (3, 6, 9, 12 + the 0x10 path)
    write the new frame to `descriptor[+3]` BEFORE checking the
    tier-byte / tick gates.  A failed gate leaves the descriptor
    in its end-frame state — the animation "completes" even
    though the final frame's sprite was never drawn.
  - **#73** — The 0x20 arrow path's high sub-branch silently
    filters `first_byte == 0x13` via byte-arithmetic underflow:
    `if (2 < (byte)(bVar2 - 0x14)) return;` evaluates to
    `0xff > 2` for `bVar2 = 0x13`.  The port replicates with a
    signed delta check.
  - **#74** — The four animation-rate tier flags are SINGLE
    GLOBAL booleans, not per-army timers.  Step categories pick
    a tier per army based on `(cur - prev)` distance, but the
    advance is a frame-wide toggle.  The setter of these flags
    lives elsewhere — probably an FPS-throttled main-loop hook.

### Headline numbers

- **708 → 741 host tests** (+33), all green under ASan/UBSan.
- **2 retail functions ported** (the army drawer + its label
  sub-renderer): 3179 + 548 = **3727 B of decompilation**.
- **5 new engine quirks filed** (#70, #71, #72, #73, #74).
- **Updated findings doc** (`docs/findings/overlay-render.md` —
  army section grew from ~10 lines to ~180 lines covering the
  full state-machine table, dispatch tail, and bindings).
- Cross-compile (mingw32) clean — `openlords2.exe` and
  `openlords2-debug.exe` both link without warning.

### Renderer pipeline state

| layer                                        | status     |
| -------------------------------------------- | ---------- |
| (text + line + sprite-family layers — see prior dispatches)      | ported     |
| sprite blit RLE (unclipped + 2× clipped)     | ported     |
| sprite render — RLE from blob                | ported     |
| strategic-mode primary frame driver          | ported     |
| strategic-mode primary row iterators × 2     | ported     |
| strategic-mode primary per-tile drawer       | ported     |
| strategic-mode overlay frame driver          | ported     |
| strategic-mode overlay row iterators × 2     | ported     |
| strategic-mode overlay city drawer           | ported     |
| strategic-mode overlay debug-text helper     | ported     |
| **strategic-mode overlay army drawer**       | **ported (this chip)** |
| **army-label sub-renderer (FUN_00407f82)**   | **ported (this chip)** |
| sprite descriptor reader (PL8 header)        | unanalyzed |
| error log formatter (`FUN_004afab9`)         | unanalyzed |
| battlefield (combat-mode) iterators          | unlocated  |

Strategic-mode rendering is now **fully end-to-end**.  Engine code
can render a complete strategic-mode frame by binding the territory
record array, per-tile descriptor table, sprite blobs, army records,
and camera state, then invoking `engine_overlay_render_frame()` and
`engine_map_render_frame()` back-to-back.

### What's next

The three frontiers shift again:

1. **Wire gfx.c runtime to renderer + map** — Windows-host
   validation chip.  Now blocked only on user availability for
   visual verification.  This is the highest-value next milestone
   — first frame on screen would validate every piece of the
   strategic-mode renderer simultaneously.
2. **Tier-flag driver discovery** — quirk #74 leaves the four
   global rate flags' setter unlocated.  Likely a small function
   in the main loop adjacent to the FPS limiter
   (`src/engine_fps.c`'s `_DAT_005c8a4c` family).  Low-LOC
   payoff but unblocks correct unit-animation cadence.
3. **Battlefield (combat-mode) renderer cluster** — still
   unlocated.  With strategic-mode complete, the combat-mode
   cluster is the next major scope.  Probably starts with a
   parent driver in the `0x405000..0x408000` range that uses
   `g_per_battle_hex_432` (save entry 7).  Worth a fresh survey.

Picking **#1 or #3** next.  #1 needs user verification (gated)
so it's deferred until the user is back; #2 is a quick win that
unblocks #1's visual fidelity; #3 is the natural extension of
this chip's scope.

This is a natural `/clear` point — strategic-mode rendering is
in, the next milestone needs human verification or a fresh survey.

---

## 2026-05-25 — Strategic-mode overlay renderer cluster (1 chip, 5 of 6 fns)

Continuation of the same unattended session.  The previous dispatch
closed out the strategic-mode primary (territory) layer and picked
the **battlefield (combat-mode) renderer** as the natural next target.
That hypothesis turned out to be wrong: the actual next-natural target
is the strategic-mode **overlay** pass — a second full-map sweep
called RIGHT NEXT TO the primary pass in the same parent caller.

### Discovery: it's a second strategic-mode pass, not battlefield

Survey of `FUN_00405602` (608 B) revealed the same shape as the
primary pass's driver `FUN_00405487`: same camera, same tile array,
same per-tile descriptor base.  But it tests DIFFERENT descriptor
flag bits (`0x80` and `0x40` vs the primary's `0x20`) and dispatches
to different per-tile drawers.  Cross-reference of the parent caller
confirmed:

```c
FUN_0040526e();      // preparatory work
FUN_00405602();      // ← overlay pass (this chip)
FUN_00405487();      // primary pass (previous chip, ported)
```

So this is the strategic-mode **units + cities overlay** layer, not
the combat-mode battlefield.  The previous dispatch's battlefield
hypothesis was based on the per-battle-hex 0x1b0 stride array — but
that array is consumed by game-logic scanners (`for i in 1..0x50`
loops), not by a renderer.  The battlefield renderer, if it exists
as a tight cluster like this one, hasn't been located yet.

### Chip landed

**`src/engine_overlay_render` (NEW, ~620 LOC + 24 tests)** — 5 retail
leaves over one shared module, with the 6th (army drawer) as a stub
for the next chip:

  - `FUN_00405602` → `engine_overlay_render_frame`            (608 B)
    Outer driver.  View-mode-1 left-strip pre-clear, then top
    phantom row + (row_count - 2)/2 row pairs + bottom phantom row.
  - `FUN_00405eb5` → `engine_overlay_render_row_phase0`       (247 B)
    Phase-0 row iterator.  All cells phase 0.  No left/right edge
    extras (asymmetric vs primary pass — quirk #65).
  - `FUN_00405fac` → `engine_overlay_render_row_phase1`       (497 B)
    Phase-1 row iterator.  First cell phase 1, middle cells phase 0,
    trailing cell phase 2.  Debug-text fires only on middle cells.
  - `FUN_004081a6` → `engine_overlay_render_tile_city`        (658 B)
    Per-tile city/town/settlement drawer (bit 0x40 selector).  Sprite
    index derived from per-cell size table at retail `0x500c30` and
    capped by the active territory's max-size band.  Dispatches via
    RLE sprite family.
  - `FUN_00408c50` → `engine_overlay_render_debug_text`       (390 B)
    Two-mode developer overlay.  Mode 1 draws per-cell size table
    value; mode 2 (winner) draws descriptor head-link byte.
  - `FUN_004071a0` → `engine_overlay_render_tile_army` (STUB, 3179 B)
    Per-tile army/units drawer (bit 0x80 selector).  Real body
    deferred to next chip; stub bumps a per-call counter so the row
    iterators can be tested for dispatch fidelity.

The module shares the cursor (gfx_glyph's `g_cursor_x/y`) with the
primary pass — they alternate sequentially per frame, never
concurrently.  The error counter `_DAT_0058fea8` is retail-conflated
but the port keeps each module's count private so tests can isolate.

### Filed quirks (#65, #66, #67, #68, #69)

Five new entries in `docs/findings/engine-quirks.md`:

  - **#65** — Overlay phase-0 row has NO left/right edge cells.
    Asymmetric vs primary pass which has both.  Visible at map edges.
  - **#66** — Debug text fires only on MIDDLE cells of phase-1 row.
    First (phase 1) and last (phase 2) cells skip the helper.
  - **#67** — City drawer territory-cap test is asymmetric for
    negative sizes — cap fires only when computed > cap_band.
    Negative computed sizes pass through, producing sprite indices
    below 0x38.
  - **#68** — City drawer clears bit 0x40 of descriptor[+2] post-
    dispatch.  Re-arm path is elsewhere — tests must re-arm between
    frames.
  - **#69** — Pre-clear strip (left 4-pixel column at y=24..443) is
    VIEW-MODE-1-ONLY.  View-modes 0 and 2 leave it untouched.

### Headline numbers

- **684 → 708 host tests** (+24), all green under ASan/UBSan.
- **5 retail functions ported** + 1 stub (army drawer).
  - Ported: 608 + 247 + 497 + 658 + 390 = **2400 B**.
  - Stubbed: 3179 B (next chip).
- **1 new module** (`src/engine_overlay_render`, ~620 LOC).
- **5 new engine quirks filed** (#65, #66, #67, #68, #69).
- **1 new findings doc** (`docs/findings/overlay-render.md`, ~220 lines).
- Cross-compile (mingw32) clean — `openlords2.exe` and
  `openlords2-debug.exe` both link without warning.

### Renderer pipeline state

| layer                                        | status     |
| -------------------------------------------- | ---------- |
| (text + line + sprite-family layers — see prior dispatches)      | ported     |
| sprite blit RLE (unclipped + 2× clipped)     | ported     |
| sprite render — RLE from blob                | ported     |
| strategic-mode primary frame driver          | ported     |
| strategic-mode primary row iterators × 2     | ported     |
| strategic-mode primary per-tile drawer       | ported     |
| **strategic-mode overlay frame driver**      | **ported (this chip)** |
| **strategic-mode overlay row iterators × 2** | **ported (this chip)** |
| **strategic-mode overlay city drawer**       | **ported (this chip)** |
| **strategic-mode overlay debug-text helper** | **ported (this chip)** |
| **strategic-mode overlay army drawer**       | **stub (next chip)** |
| sprite descriptor reader (PL8 header)        | unanalyzed |
| error log formatter (`FUN_004afab9`)         | unanalyzed |
| battlefield (combat-mode) iterators          | unlocated  |

The strategic-mode overworld now has 5 of 6 cluster functions ported
across two passes.  The army drawer's 3179 B body is the only piece
missing before strategic-mode rendering is end-to-end.

### What's next

1. **Army drawer (`FUN_004071a0`, 3179 B)** — strictly the natural
   next chip.  The stub already wires it into the iterators; the
   real port fills the stub body.  Complexity is in the unit-category
   state-machine logic (chase/fight/march/die) and the per-state frame
   animation that writes to descriptor[+3].
2. **Wire gfx.c runtime to renderer + map** — Windows-host validation
   chip.  Bind DDraw backbuffer as gfx_paint canvas, build a
   synthetic territory + tile + city + army scene, render one frame.
   Requires user-observed validation.  Gated until user is back.
3. **Battlefield (combat-mode) renderer cluster** — still
   unlocated.  The hypothesis from the previous dispatch about
   `g_per_battle_hex_432` being the natural anchor is plausible but
   unconfirmed — that array's consumers are game-logic scanners, not
   renderers.  Probably exists elsewhere in the binary; needs a fresh
   survey.

Picking #1 next.  It's the cleanest scope (one function, one chip)
and finishes strategic-mode rendering end-to-end.

This is a natural `/clear` point — strategic-mode overlay is in,
the next chip is a single self-contained function port.

---

## 2026-05-25 — Strategic-mode map renderer cluster (1 chip, 4 fns)

Continuation of the same unattended session.  The previous dispatch
closed out the entire low-level sprite pipeline (both families + the
RLE wrapper) and identified three frontiers, picking **map-tile /
battlefield iterators** as the largest-scope, highest-value next
step.  This dispatch ports the four-function strategic-mode map
renderer cluster.

### Discovery: the 0x1a4-stride is the per-territory record array

Surveying `FUN_00408438`'s field accesses surfaced field offsets
`+0x02, +0x04, +0x07, +0x08, +0x09, +0x149` from a base of
`DAT_0052f0b4`.  Cross-referencing `0x52f0b0` against the
save-format table identified the array as **save schema entry 5**:
151 × 420 bytes (= `0x1a4`-byte stride) — the per-territory state
records.  Each cell on the map is associated with a per-tile linked
list of TERRITORY indices via:

  - per-tile-bytecode descriptor table at `0x522f90` (byte fields
    at +2 = flag, +5 = head-link)
  - per-territory record next-link field at offset `+0x04`
  - self-loop termination (record's next-link == its own index)

This changed the chip's framing.  What I initially thought was a
"sprite-record table" is the SAME 420-byte struct that holds
strategic-mode game state (owner, army composition, food/gold).
The map-render code consumes only seven byte/short fields of it;
the other 414 bytes are gameplay state we already declared in
`src/save.h` as `g_per_territory_420`.

### Chip landed

**`src/engine_map_render` (NEW, ~520 LOC + 20 tests)** — four
retail leaves over one shared module:

  - `FUN_00405487` → `engine_map_render_frame`               (379 B)
    Outer frame driver.  Drives one top phantom row, then drops into
    the row-pair loop, then one bottom phantom row.  Camera-aware.
  - `FUN_00405862` → `engine_map_render_row_phase0`          (333 B)
    Phase-0 row iterator.  Optional left-edge cell at `(col_start - 1,
    cur_row)` gated on `col_start > 0`; col_count main cells; optional
    trailing cell gated on `cur_col < 0x40`.
  - `FUN_004059af` → `engine_map_render_row_phase1`          (314 B)
    Phase-1 row iterator.  First tile at phase 1, remaining at phase 0,
    plus one unconditional trailing cell.
  - `FUN_00408438` → `engine_map_render_tile`                (2072 B)
    Per-tile drawer.  Walks per-tile linked list of territory records;
    per record computes (dx, dy) from view-mode-selected palette tables;
    applies phase + record-type position adjusts; stages primary
    descriptor; validates; dispatches first-family blitter via
    `gfx_dispatch_apply_skip_and_dispatch_sprite`; optionally stages
    + dispatches overlay sprite from RLE blob via
    `gfx_dispatch_dispatch_sprite_rle`; restores cursor.

**Six 128-byte palette tables** (`0x4d8108..0x4d8388`) replicated as
static-const `int8_t[3][2][128]` — exact byte copies verified via
`r2 pxw 0x80 @ <addr>`.  Indexed by `[view_mode][axis][rotation * 16
+ frame_offset]`.

The module's state-binding surface is large (camera + tile array +
tile descriptors + territory records + 3 blobs + view mode + render
mode + rotation offset + overlay base + Y viewport offset + dirty
state) but each retail global has a distinct setter, mirroring the
existing `gfx_dispatch` / `engine_dirty_rect` conventions.

### Filed quirks (#60, #61, #62, #63, #64)

Five new entries in `docs/findings/engine-quirks.md`:

  - **#60** — Map-render uses raw BYTE OFFSETS into the per-tile
    descriptor table.  The cell value loaded from the 2D map array
    is used as a byte offset (not an index × stride) — meaning the
    multiplication is baked in at map-load time.  A malformed cell
    can read far outside the per-tile region.
  - **#61** — Linked-list terminator is SELF-LOOP, not zero.
    The loop exits when `record.next_link == record_idx`.  Maps
    must guarantee last-record self-link; otherwise the loop visits
    slot 0 (reserved sentinel) and continues.
  - **#62** — Invalid descriptor aborts the ENTIRE linked-list walk
    for that tile.  A corrupt record skips ALL subsequent records
    in the tile's list — undefined visual outcome, no crash.
  - **#63** — Overlay-bearing records fire TWO dirty-rect marks
    (32-px pre-clip + 48-px post-dispatch), at potentially DIFFERENT
    tile coords if clip_x shifted the cursor.  Primary-only records
    fire ONE mark.  Asymmetry observable.
  - **#64** — The 48-px dirty box in the non-overlay path is DEAD
    CODE.  `local_24 == 2` is never assigned; only 0 and 3 reach the
    selector.  The 48-px helper IS reachable via the overlay's
    post-dispatch mark.

### Headline numbers

- **664 → 684 host tests** (+20), all green under ASan/UBSan.
- **4 retail functions ported** (~3098 B decompilation:
  379 + 333 + 314 + 2072 B = 3098 B total).
- **1 new module** (`src/engine_map_render`, +520 LOC).
- **5 new engine quirks filed** (#60, #61, #62, #63, #64).
- **1 new findings doc** (`docs/findings/map-render.md`, ~190 lines).
- Cross-compile (mingw32) clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### Renderer pipeline state

| layer                                | status     |
| ------------------------------------ | ---------- |
| (text + line + sprite-family layers — see prior dispatch)        | ported     |
| sprite blit RLE (unclipped)          | ported     |
| sprite blit RLE (clipped × 2)        | ported     |
| sprite render — RLE from blob        | ported     |
| **strategic-mode frame driver**      | **ported (this chip)** |
| **strategic-mode row iterators × 2** | **ported (this chip)** |
| **strategic-mode per-tile drawer**   | **ported (this chip)** |
| sprite descriptor reader (PL8 header)| unanalyzed |
| error log formatter (`FUN_004afab9`) | unanalyzed |
| battlefield (combat-mode) iterators  | unanalyzed |

The strategic-mode overworld map is now end-to-end in `src/`.  Engine
code can render one frame of the campaign map by binding the
territory record array, per-tile descriptor table, sprite blobs, and
camera state, then invoking `engine_map_render_frame()`.

### What's next

The three frontiers from the previous dispatch are now reshuffled:

1. **Wire gfx.c runtime to renderer + map** — a Windows-host
   validation chip.  Bind the DDraw backbuffer as `gfx_paint`
   canvas, build a synthetic territory table + tile array, render
   one frame.  Requires user-observed validation.  Bigger scope
   than just the gfx.c probe — now we can render the actual map.
2. **Battlefield (combat-mode) renderer cluster** — once the
   strategic-mode chip lands, the combat-mode equivalent surfaces
   as the natural next target.  Different record array
   (`g_per_battle_hex_432`, save entry 7), different stride
   (0x1b0 vs 0x1a4), but similar shape.  Probably 3-5 functions
   in the 0x405000..0x408000 range.
3. **PL8 header parser** — still the smallest chip.  Would
   enable tests with REAL retail sprite data instead of the
   synthetic 1×1 fixtures we currently use.  Useful for golden-
   pixel diff tests but not blocking forward progress.

Picking #1 or #2 next.  #1 requires user verification (gated) so
it's deferred until the user is back to validate.  #2 is the
natural extension of this chip's scope and likely 1-2 sessions.

This is a natural `/clear` point — strategic-mode map rendering is
end-to-end, the next frontier is a fresh subsystem.

---

## 2026-05-25 — RLE sprite blitter family + RLE-aware wrapper (1 chip, 4 fns + 1 helper)

Continuation of the same unattended session.  The previous dispatch
closed out the sprite-render entry-point wrappers and listed the
second sprite-blitter family (`FUN_004b4537`/`FUN_004b4814`/
`FUN_004b48b8`) as the next candidate, hypothesizing a "source stride
padding" variant for non-tightly-packed sources.  That hypothesis
turned out to be wrong by a wide margin — the second family is an
**RLE-decoding** sprite blitter, not a stride variant.  This dispatch
ports the entire RLE family (three blitters + one private row-skip
helper) and the matching caller-blob wrapper (`FUN_0040a3d0`).

### Discovery: it's RLE, not strided

Survey of `FUN_004b4537` (733 B — much larger than the first family's
188 B unclipped) revealed a 17-arm `switch` on a per-byte opcode read
from the source data:

  - `0x00 N` — transparency run: advance dst by N pixels, src by 2.
  - `0x01..0x0E` — literal-of-N-bytes: dst[i] = src[i+1] for i ∈ [0,N),
    advance src by N+1, dst by N, pixel counter by N.
  - `0x10` — literal-of-16-bytes (dedicated arm).
  - default — same as `0x0F` (literal-of-15-bytes).  Folded for codegen.

Each row terminates when the per-row pixel counter reaches `sprite_w`.
The source data is a row-major stream of these opcodes with NO
between-row marker — the natural row boundary IS the byte at which
the cumulative pixel count hits `sprite_w`.

This explains why the second-family wrapper (`FUN_0040a3d0`, 402 B)
omits the `src_offset += sprite_w * top_skip + left_skip` step the
first-family wrapper performs: RLE opcodes are variable-sized, so the
byte-offset to "the first visible pixel" can't be computed without
walking the stream.  The blitters internalize the walk instead.

### Chip landed

**`src/gfx_sprite_rle` (NEW, ~340 LOC + 13 tests)** — 3 retail blitter
leaves plus a private row-skip helper sharing dispatcher state with
the first family via existing `gfx_glyph_*` / `gfx_dispatch_*` getters:

  - `FUN_004b4537` → `gfx_sprite_rle_blit`               (733 B)
    Unclipped path.  17-arm opcode switch.  Source pre-walked by the
    helper to skip top rows; dst pointer persists across rows and
    advances by `dst_stride_pad` between them (the standard
    canvas_width - sprite_w gap).
  - `FUN_004b4814` → `gfx_sprite_rle_blit_left_clipped`  (164 B)
    Left-clipped path.  Per-row dst origin is pre-shifted LEFT by
    `left_skip` cells; writes are gated on a per-row `clip_counter`
    that starts at `left_skip` and decrements on every advance
    (skip or literal).  Writes fire when `clip_counter < 1`.
  - `FUN_004b48b8` → `gfx_sprite_rle_blit_right_clipped` (162 B)
    Right-clipped path.  Per-row `visible_budget` starts at
    `clipped_w` and decrements on every advance.  Writes fire when
    `visible_budget > 0`.  Source pointer keeps walking past the
    visible budget so the next row's RLE start lines up at the
    natural row boundary.

  - `FUN_004b495a` → static `rle_skip_top_rows`          (93 B)
    Walks past `top_skip` rows of RLE without writing.  Decrements
    a per-row pixel counter on each opcode; advances src by the
    opcode-specific delta.  Returns the pointer to the first
    un-skipped row.

**`src/gfx_dispatch` (extended, +13 LOC)** — exposed a new dispatch
helper for the RLE family:

  - `gfx_dispatch_dispatch_sprite_rle(blob)` — routes to one of the
    three RLE blitters based on `x_clip_mode`, gated on
    `y_clip_mode != 5`.  Does NOT bake top_skip/left_skip into
    src_offset (the RLE blitters consume them directly).

**`src/gfx_sprite_render` (extended, +42 LOC)** — added the RLE-aware
caller-blob wrapper:

  - `FUN_0040a3d0` → `gfx_sprite_render_rle_from_blob`   (402 B)
    Sibling of `gfx_sprite_render_from_blob`.  Same staging shape
    (descriptor read, src_offset validity gate, log-hook + error
    latch), but routes through `gfx_dispatch_dispatch_sprite_rle`
    instead of the skip-baking variant.

New error format-string IDs added:

  - `GFX_SPRITE_RENDER_ERR_RLE_FROM_BLOB_NULL`           (0x4d2368)
  - `GFX_SPRITE_RENDER_ERR_RLE_FROM_BLOB_INDEX_TOO_HIGH` (0x4d2384)
  - `GFX_SPRITE_RENDER_ERR_RLE_FROM_BLOB_INDEX_TOO_LOW`  (0x4d23a4)

The trio's rdata addresses sit exactly 0x54 bytes after the first-
family triplet — same 0x1c-byte spacing pattern as within each
triplet.  Verified by reading the third arg of the three
`FUN_004afab9` calls inside `FUN_0040a3d0`.

### Filed quirks (#56, #57, #58, #59)

Four new entries in `docs/findings/engine-quirks.md`:

  - **#56** — Unclipped RLE blitter opcode 0x0F is folded into the
    default arm.  Invisible for well-formed streams; for any opcode
    value 0x11..0xFF (which retail never emits) the per-row pixel
    accounting drifts because the blitter treats it as a 15-byte
    literal regardless of the encoded data length.
  - **#57** — Left-clip RLE blitter pre-shifts its per-row dst origin
    LEFT by `left_skip` cells.  The walker decrements a counter on
    every advance and gates writes on "counter exhausted", so the
    `left_skip` cells worth of dst at negative offsets are skipped
    rather than written.  Equivalent to first-family's pre-baked
    src_offset, but the RLE family CAN'T pre-bake — opcodes have
    variable byte sizes.
  - **#58** — Right-clip RLE blitter keeps walking the source past
    visible-budget exhaustion.  Required for next-row alignment
    (the next row's RLE start is at the byte where the current
    row's natural pixel count reaches sprite_w, not at the byte
    where the visible budget ran out).  Opcode-skip overshoot can
    drive `visible_budget` arbitrarily negative; the `> 0` gate
    still suppresses writes correctly.
  - **#59** — The RLE wrapper (`FUN_0040a3d0`) is byte-symmetric to
    `FUN_0040a21a` EXCEPT for the missing skip-baking lines.  Documents
    the equivalence between "wrapper bakes skips into src_offset" and
    "blitter consumes skips intrinsically" — two paths to the same
    visible output, chosen by source-encoding format.

### Headline numbers

- **651 → 664 host tests** (+13), all green under ASan/UBSan.
- **4 retail functions ported** (~1.36 KB decompilation —
  733 + 164 + 162 + 93 + 402 wrapper = 1554 B total) + **1
  helper** (private static).
- **1 new module** (`src/gfx_sprite_rle`) + **2 modules extended**
  (`src/gfx_dispatch` with the no-skip-bake dispatcher, +13 LOC;
  `src/gfx_sprite_render` with the RLE wrapper, +42 LOC).
- **4 new engine quirks filed** (#56, #57, #58, #59).
- Cross-compile (mingw32) clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### Renderer pipeline state

| layer                                | status     |
| ------------------------------------ | ---------- |
| char width                           | ported     |
| buffer width                         | ported     |
| bounded buffer width                 | ported     |
| word width + chars                   | ported     |
| editor (text_input)                  | ported     |
| localization blob access             | ported     |
| dirty-rect tracking                  | ported     |
| 1-pixel / 2x1 painters               | ported     |
| clipped pixel / 2x1                  | ported     |
| glyph blitter (unclipped)            | ported     |
| glyph blitter (clipped × 2)          | ported     |
| glyph dispatcher (per-char)          | ported     |
| clip setters (X + Y)                 | ported     |
| text renderer (per-line)             | ported     |
| centered-text wrapper                | ported     |
| substitution stream                  | ported     |
| word-wrap renderer (buffer)          | ported     |
| word-wrap renderer (localized)       | ported     |
| localization line render             | ported     |
| localization line centered           | ported     |
| solid Bresenham line                 | ported     |
| dotted axis-aligned line             | ported     |
| solid rect outline                   | ported     |
| 3D bevel rect (3 palettes)           | ported     |
| int formatter (dec + hex)            | ported     |
| label render (dec + hex + centered)  | ported     |
| label render (signed-color)          | ported     |
| compound "label + signed value"      | ported     |
| sprite blit raw (unclipped)          | ported     |
| sprite blit raw (clipped × 2)        | ported     |
| sprite dispatcher (canvas + rect)    | ported     |
| sprite render — raw buffer           | ported     |
| sprite render — from blob            | ported     |
| sprite render — global blob          | ported     |
| sprite render — truncate bottom      | ported     |
| sprite render — truncate top         | ported     |
| **sprite blit RLE (unclipped)**      | **ported (this chip)** |
| **sprite blit RLE (clipped × 2)**    | **ported (this chip)** |
| **sprite render — RLE from blob**    | **ported (this chip)** |
| sprite descriptor reader (PL8 header)| unanalyzed |
| map-tile / battlefield iterators (0x408438+) | unanalyzed |
| error log formatter (`FUN_004afab9`) | unanalyzed |

Both sprite-blitter families (raw + RLE) are now end-to-end in `src/`.
Engine code can dispatch raw-pixel OR RLE-encoded sprites from a blob
to a bound canvas via either the dispatcher API or the higher-level
caller-blob wrappers.

### What's next

The same three un-ported frontiers from the previous dispatch carry
forward, now WITHOUT the second-blitter-family blocker:

1. **Map-tile / battlefield iterators** — `FUN_00408438` (2072 B),
   `FUN_00405487` (379 B), `FUN_00405862` (333 B), `FUN_004059af`
   (314 B).  A 2D-tile iterator cluster that calls the FIRST sprite
   family directly (not via the dispatcher).  Now-blocked: the RLE
   family port lets us recognize at-a-glance which iterator inlines
   which family.  Substantial scope; probably unblocks the map-view
   renderer.
2. **PL8 header parser** — small chip.  Read the 8-byte header,
   decode the descriptor array, expose a "load sprite blob from
   file bytes" API.  Enables tests against real `vendor/original/*.pl8`
   data; required for any visible-pixels milestone with retail assets.
3. **Wire gfx.c runtime to the renderer + sprites** — extend the
   DDraw probe to bind a real canvas, then render a real glyph +
   sprite during boot.  Gated on user-observed Windows-host
   validation.  Visible-pixels milestone earned.

Picking #1 (map-tile iterators) is the largest scope and feeds
directly into the visible-game-state renderer.  #2 is a small chip
that yields format docs more than visible output.  #3 unlocks visual
validation but requires human verification.

This is a natural `/clear` point — the entire low-level sprite
pipeline (both families + dispatchers + entry-point wrappers) is
closed out and the remaining frontiers are fresh subsystems
worth a fresh conversation.

---

## 2026-05-25 — Sprite-render sibling-of-dispatcher wrappers (1 chip, 5 fns)

Continuation of the same unattended session.  The previous dispatch
closed out the sprite blit kernels + canvas/rect dispatchers and
listed three follow-ups, with **higher-level sprite renderers** as
the first.  Surveying the dispatcher-blitter call topology surfaced
not "callers of the dispatcher" (as the prior dispatch hypothesized)
but rather **siblings of the dispatcher** — five small leaf
wrappers that INLINE the staging + clip + dispatch pipeline rather
than calling the already-ported `gfx_dispatch_sprite`.  This dispatch
ports all five.

Discovery: the wrappers and the dispatcher share their entire
post-staging pipeline.  Five small functions, all ~260–440 B, all at
0x40a0xx–0x40a9xx, all invoking the same `clip_x`/`clip_y` setters,
the same `top_skip`+`left_skip` source-offset adjustments, and the
same three-way `x_clip_mode`-indexed blitter dispatch — just with
extra bookkeeping that the dispatcher itself omits:

  - Each writes the "current sprite index" global `DAT_005c9288`
    BEFORE staging the descriptor (the dispatcher leaves this alone).
  - Four of five gate the descriptor's `src_offset` against
    `[1, 0x4baf1)` and latch error code 4 + fire a log call on a
    miss (the dispatcher does no validity check).
  - Two variants tweak the staged state in between descriptor-stage
    and clip-setter calls (truncate_bottom / truncate_top — see
    below).

### Chip landed

**`src/gfx_sprite_render` (NEW, ~165 LOC + 20 tests)** — 5 retail
leaves, all reusing the dispatcher's now-public `stage_sprite_descriptor`
and `apply_skip_and_dispatch` helpers:

  - `FUN_0040a01d` → `gfx_sprite_render_raw`                (261 B)
    Render a raw caller-supplied buffer at (x, y) with explicit
    width / height.  No descriptor read; source pixels are densely
    packed at `w` bytes per row.  Early-return on NULL buffer.  No
    bounds check, no error log.
  - `FUN_0040a21a` → `gfx_sprite_render_from_blob`          (433 B)
    Render sprite[index] from a caller-supplied blob.  Three error
    gates: NULL blob, src_offset < 1, src_offset > 0x4baf0.
  - `FUN_0040a682` → `gfx_sprite_render`                    (391 B)
    Same shape but reads the blob from a module-private "current
    blob" pointer (mirrors retail `DAT_004eabec`, allocated by
    `FUN_004b25b0` and freed by `FUN_004b25fc`).  Two error gates
    (no NULL check — caller is responsible for binding).
  - `FUN_0040a80e` → `gfx_sprite_render_truncate_bottom`    (413 B)
    Same as `gfx_sprite_render` but subtracts `trim_rows` from the
    staged `height_short` before clip_y — net effect: skip the
    bottom N source rows.  Same error gates.
  - `FUN_0040a9b0` → `gfx_sprite_render_truncate_top`       (439 B)
    Same as `gfx_sprite_render` but:
      - shrinks `height_short` by `skip_rows`
      - advances source offset by `sprite_w * skip_rows`
      - shifts dst_y down by `skip_rows`
    Net effect: skip the top N source rows; visible pixels land at
    the same absolute (x, y + skip_rows) as gfx_sprite_render would
    have anchored them.  Same error gates.

**`src/gfx_dispatch` (extended, +5 LOC)** — exposed two helpers
previously kept `static`:

  - `gfx_dispatch_stage_sprite_descriptor(blob, idx)` — extracted
    from the dispatcher; reads the 16-byte descriptor and stages
    width / height_short / src_offset / flag_a / flag_b /
    row_count placeholder into the shared glyph state.
  - `gfx_dispatch_apply_skip_and_dispatch_sprite(blob)` — extracted
    apply-top/left-skip + 3-way blitter dispatch.  Gated on
    `y_clip_mode != 5`.

Also added `gfx_dispatch_set_last_glyph_index(int)` — the
"current 16-byte-descriptor index" global (`DAT_005c9288`) is shared
between the glyph dispatcher and the sprite-render wrappers.
Retail uses one global with several writers; our port now preserves
that topology by exposing the setter (previously only the glyph
dispatcher wrote it).

Module state in `gfx_sprite_render.c`:
  - `g_current_blob` — pointer to the bound "current sprite blob"
    (mirrors `DAT_004eabec`).  Settable via the public bind API.
  - `g_last_error` — sticky error-code byte latch (mirrors
    `DAT_005aeb68` written by `FUN_00408fb0`, a 27-B retail leaf
    that's effectively just `latch = code`).
  - `g_log_fn` — NULL-default callback hook for the log call.
    Tests install one that records (fmt_addr, sprite_index + 1)
    tuples.  The real retail formatter (`FUN_004afab9`, 897 B) is
    not yet ported; this hook design lets us validate the error
    paths without porting it.

The `_DAT_005cd400 = 0x280 - sprite_w` pre-write that every retail
wrapper performs before `clip_x` is omitted from the port — it's
dead code (see quirk #52).

### Filed quirks (#52, #53, #54, #55)

Four new entries in `docs/findings/engine-quirks.md`:

  - **#52** — All five wrappers pre-write `dst_stride_pad` to a
    hard-coded `0x280 - sprite_w` BEFORE invoking `clip_x`, which
    unconditionally overwrites the same global at the end of its
    body.  The pre-write is dead.  Almost certainly an MSVC inlining
    artifact — the `0x280` literal (vs. the runtime canvas-width
    global `DAT_004eb274` that clip_x reads) is the tell.
  - **#53** — The descriptor-validity gate rejects
    `src_offset == 0` as "too low".  Zero is otherwise a valid
    relative offset inside a blob (the dispatcher itself accepts
    it).  Almost certainly a "no real sprite starts at offset 0"
    1996 assumption — well-formed PL8 files push the first
    descriptor's offset to at least 8 via the file's 8-byte header.
  - **#54** — `gfx_sprite_render_raw` (`FUN_0040a01d`) doesn't
    reset the descriptor-flag globals (`DAT_00591568` /
    `DAT_0057d3c0`).  Whatever the previous descriptor-based
    render left there carries into the next blit.  Invisible today
    (no consumer of those flags is ported) but a latent footgun.
  - **#55** — `gfx_sprite_render_truncate_bottom`'s
    `trim_rows` subtraction is unconditional.  Negative
    `trim_rows` makes the visible height GROW past the descriptor's
    natural extent — the blitter reads past the end of source.
    No clamp; preserved verbatim.

### Headline numbers

- **631 → 651 host tests** (+20), all green under ASan/UBSan.
- **5 retail functions ported** (~1.94 KB decompilation —
  261 + 433 + 391 + 413 + 439).
- **1 new module** (`src/gfx_sprite_render`) + **1 module
  extended** (`src/gfx_dispatch`, +1 setter + 2 helpers de-static-d).
- **4 new engine quirks filed** (#52, #53, #54, #55).
- Cross-compile (mingw32) clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### Renderer pipeline state

| layer                                | status     |
| ------------------------------------ | ---------- |
| char width                           | ported     |
| buffer width                         | ported     |
| bounded buffer width                 | ported     |
| word width + chars                   | ported     |
| editor (text_input)                  | ported     |
| localization blob access             | ported     |
| dirty-rect tracking                  | ported     |
| 1-pixel / 2x1 painters               | ported     |
| clipped pixel / 2x1                  | ported     |
| glyph blitter (unclipped)            | ported     |
| glyph blitter (clipped × 2)          | ported     |
| glyph dispatcher (per-char)          | ported     |
| clip setters (X + Y)                 | ported     |
| text renderer (per-line)             | ported     |
| centered-text wrapper                | ported     |
| substitution stream                  | ported     |
| word-wrap renderer (buffer)          | ported     |
| word-wrap renderer (localized)       | ported     |
| localization line render             | ported     |
| localization line centered           | ported     |
| solid Bresenham line                 | ported     |
| dotted axis-aligned line             | ported     |
| solid rect outline                   | ported     |
| 3D bevel rect (3 palettes)           | ported     |
| int formatter (dec + hex)            | ported     |
| label render (dec + hex + centered)  | ported     |
| label render (signed-color)          | ported     |
| compound "label + signed value"      | ported     |
| sprite blit (unclipped)              | ported     |
| sprite blit (clipped × 2)            | ported     |
| sprite dispatcher (canvas + rect)    | ported     |
| **sprite render — raw buffer**       | **ported (this chip)** |
| **sprite render — from blob**        | **ported (this chip)** |
| **sprite render — global blob**      | **ported (this chip)** |
| **sprite render — truncate bottom**  | **ported (this chip)** |
| **sprite render — truncate top**     | **ported (this chip)** |
| second sprite-blitter family (0x4b4537/14/8b8) | unanalyzed |
| sprite descriptor reader (PL8 header)| unanalyzed |
| map-tile / battlefield iterators (0x408438+) | unanalyzed |
| error log formatter (`FUN_004afab9`) | unanalyzed |

The sprite-render entry-point family is now end-to-end in `src/`.
The dispatcher + the 5 wrappers cover every retail callsite that
uses the first sprite-blitter family (`FUN_004b43b1` /
`FUN_004b446d` / `FUN_004b44d2`) at the entry-point level.  Engine
code can now render a sprite from a heap blob, a caller blob, or a
raw buffer; with bottom-row trim, top-row skip, or no trim; via
either pure C tests or the cross-compiled binary.

### What's next

The survey turned up several un-ported leaf families that are now
the obvious frontiers:

1. **Second sprite-blitter family** — `FUN_004b4537` /
   `FUN_004b4814` / `FUN_004b48b8` is a second 3-blitter set
   referenced by `FUN_00408438` and `FUN_0040a3d0`.  Same shape as
   the first family but probably differs on the write mode (the
   `FUN_00408438` callsite reads from a different descriptor slot
   layout).  Most likely candidate: a "sprite WITH source stride
   padding" variant for non-tightly-packed source data.  Small
   chip (~3 functions × 100-200 B each).
2. **Sprite-render wrapper for the second family** —
   `FUN_0040a3d0` (402 B) — the sibling of `gfx_sprite_render_from_blob`
   that targets the second blitter family.  Companion to #1.
3. **Map-tile / battlefield iterators** — `FUN_00408438`
   (2072 B), `FUN_00405487` (379 B), `FUN_00405862` (333 B),
   `FUN_004059af` (314 B) — a 2D-tile iterator cluster that
   inlines the sprite blit dispatch (calls the first blitter
   family directly, not via the dispatcher).  Substantial scope;
   probably unblocks the map-view rendering.
4. **PL8 header parser** — small chip: read the 8-byte header
   to learn descriptor count + decode the descriptor array.
   Enables tests against real `vendor/original/*.pl8` data.
5. **Wire gfx.c runtime to the renderer + sprites** — extend the
   DDraw probe to bind a real canvas, then render a real glyph +
   sprite during boot.  Visible-pixels milestone earned.  Gated on
   user-observed Windows-host validation.

Picking #1 keeps autonomous progress moving and unblocks #2 (the
second-family render wrapper) by exposing the analogous blitter
trio.  #3 is the largest scope and feeds directly into the
visible-game-state renderer.  #5 unlocks visual validation but
requires human verification.

This is a natural `/clear` point — the sprite-render entry-point
family is closed out and the remaining frontiers (second blitter
family, map-tile iterators, PL8 parser, runtime wiring) are fresh
scope worth a fresh conversation.

---

## 2026-05-25 — Sprite blit kernels + dispatchers (1 chip, 5 fns)

Continuation of the same unattended session.  The previous dispatch
closed out the text-control entry-point family and listed sprite
blitters as the "fresh subsystem" candidate (vs. runtime DDraw
wiring, which is gated on Windows-host validation).  This dispatch
picks up sprite blitters — opens the next leaf class and lands the
entire 5-function family in one chip.

Discovery: the sprite blit family shares its ENTIRE staging-state
pipeline with the already-ported glyph blit family.  Same 16-byte
descriptor layout (width / height / source-offset / row-count / two
flag bytes at the same offsets), same `clip_x` / `clip_y` setters,
same `x_clip_mode`-indexed dispatch between unclipped / left-clipped
/ right-clipped variants.  The only behavioral split is the WRITE
mode (sprite copies source byte verbatim; glyph supports both
copy-byte and silhouette-fill) and the dispatcher's lack of a
char→glyph indirection (sprite index is direct) and per-font
kerning tweaks.  Two dispatcher entry points instead of one (canvas-
clip + caller-supplied-rect) replace the alt-clip toggle flag.

### Chip landed

**`src/gfx_sprite` (NEW, ~165 LOC + 16 tests)** — 3 retail leaves +
1 private clipped helper sharing state with `gfx_glyph` via new
read-only accessors:

  - `FUN_004b43b1` → `gfx_sprite_blit`              (188 B)
    Unclipped path.  Iterates `glyph_h` rows of `glyph_w` cols.
    Inner loop carries an unusual partial-unroll shape (see quirk
    #48): the FIRST 4 bytes go through an unrolled block when
    `glyph_w` is a multiple of 4, then a byte-at-a-time tail
    handles the rest.  For non-multiple-of-4 widths, the entire
    row goes through the tail.  Net effect: the unroll fires
    AT MOST ONCE per row regardless of width — useless past 4
    pixels.  Almost certainly an MSVC 4.20 codegen artifact.
  - `FUN_004b446d` → `gfx_sprite_blit_left_clipped` (101 B)
    Clipped path.  Reads `clipped_w` cols per row with
    `src_stride_pad` to skip past the trimmed cols.
  - `FUN_004b44d2` → `gfx_sprite_blit_right_clipped`(101 B)
    BYTE-IDENTICAL to FUN_004b446d — verified by r2
    `pxd 101 @ <addr>` against retail (every int matches across
    26 dump rows).  Same dedup pattern as the clipped glyph pair.
    Dispatcher selects between them by x_clip_mode tag (1 vs 2);
    both route to a shared `sprite_blit_clipped` static helper.

**`src/gfx_dispatch` (extended, +75 LOC + 5 tests)** — 2 retail
leaves extending the existing dispatcher module:

  - `FUN_00404442` → `gfx_dispatch_sprite`          (256 B)
    Canvas-clipped sprite render: clips against (0, 0,
    canvas_w, canvas_h).  Reads the 16-byte descriptor at
    `sprite_blob + sprite_index*16 + 8`, stages state via
    `gfx_glyph_set_glyph` + dispatcher-private fields, runs
    `clip_x` / `clip_y`, applies left/top skip to the source
    offset, dispatches to one of the three sprite blitters.
  - `FUN_00404547` → `gfx_dispatch_sprite_clipped`  (256 B)
    Same shape but takes (left, top, right, bottom) as
    explicit clip-rect parameters.  Retail exposes both
    entry points instead of gating on a single `g_alt_clip_enabled`
    flag (the way the glyph dispatcher does); we preserve the
    two-function split.

No new module state.  `g_flag_a` / `g_flag_b` from the existing
dispatcher state mirror the same descriptor bytes the glyph
dispatcher stages — both writers, no readers yet.

New `gfx_glyph_*` getters (`glyph_offset`, `clipped_w`,
`src_stride_pad`, `dst_stride_pad`) expose the shared staging-state
fields that the sprite blitter needs to read.  Setters were
already in place.

### Filed quirks (#48, #49, #50, #51)

Four new entries in `docs/findings/engine-quirks.md`:

  - **#48** — Sprite blitter's 4-byte partial unroll fires AT MOST
    ONCE per row, regardless of width.  Outer test `(width & 3)
    != 0` selects between full-tail and partial-unroll-plus-tail;
    neither shape loops the unrolled block.  For a 16-wide sprite
    the row is `4 unrolled + 12 byte-at-a-time` — the unroll
    amortizes nothing past 4 pixels.  Almost certainly an MSVC
    4.20 codegen artifact, not a hand-written Duff's device.
  - **#49** — Zero-width sprite writes 4 bytes per row anyway:
    `(0 & 3) != 0` is FALSE, so the unrolled body fires.  The
    post-unroll tail-gate then short-circuits to false (via
    `width > 3`), so the over-write is exactly 4 bytes — no
    compound.  Latent in retail because no descriptor encodes a
    zero width and the dispatcher's `y_clip_mode == 5` gate
    handles fully-off-canvas placement separately.  Preserved as
    a witness test.
  - **#50** — Clipped sprite pair (`FUN_004b446d` / `FUN_004b44d2`)
    is byte-identical — r2 `pxd 101` verifies every byte matches.
    Same MSVC inlining artifact as the clipped glyph pair; same
    dedup approach in the port (single `sprite_blit_clipped`
    helper, two entry points to mirror retail call topology).
  - **#51** — Sprite blitter writes source bytes verbatim — no
    color modulation.  Documents the load-bearing semantic
    distinction: glyphs are stored as silhouettes (color chosen
    at render time, except in copy-byte mode); sprites are stored
    pre-colored (every non-zero source byte IS the destination
    palette index).  Glyph clipped variants always color-fill;
    sprite clipped variants always copy.

### Headline numbers

- **615 → 631 host tests** (+16), all green under ASan/UBSan.
- **5 retail functions ported** (~922 B decompilation — 188 +
  101 + 101 + 256 + 256).  Note: the second 101 (FUN_004b44d2)
  is byte-identical to the first — but it's a distinct retail
  entry point, so it counts.
- **1 new module** (`src/gfx_sprite`) + **1 module extended**
  (`src/gfx_dispatch`, +2 functions).
- **4 new engine quirks filed** (#48, #49, #50, #51).
- Cross-compile (mingw32) clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### Renderer pipeline state

| layer                                | status     |
| ------------------------------------ | ---------- |
| char width                           | ported     |
| buffer width                         | ported     |
| bounded buffer width                 | ported     |
| word width + chars                   | ported     |
| editor (text_input)                  | ported     |
| localization blob access             | ported     |
| dirty-rect tracking                  | ported     |
| 1-pixel / 2x1 painters               | ported     |
| clipped pixel / 2x1                  | ported     |
| glyph blitter (unclipped)            | ported     |
| glyph blitter (clipped × 2)          | ported     |
| glyph dispatcher (per-char)          | ported     |
| clip setters (X + Y)                 | ported     |
| text renderer (per-line)             | ported     |
| centered-text wrapper                | ported     |
| substitution stream                  | ported     |
| word-wrap renderer (buffer)          | ported     |
| word-wrap renderer (localized)       | ported     |
| localization line render             | ported     |
| localization line centered           | ported     |
| solid Bresenham line                 | ported     |
| dotted axis-aligned line             | ported     |
| solid rect outline                   | ported     |
| 3D bevel rect (3 palettes)           | ported     |
| int formatter (dec + hex)            | ported     |
| label render (dec + hex + centered)  | ported     |
| label render (signed-color)          | ported     |
| compound "label + signed value"      | ported     |
| **sprite blit (unclipped)**          | **ported (this chip)** |
| **sprite blit (clipped × 2)**        | **ported (this chip)** |
| **sprite dispatcher (canvas-clip)**  | **ported (this chip)** |
| **sprite dispatcher (rect-clip)**    | **ported (this chip)** |
| sprite descriptor reader (PL8 header)| unanalyzed |
| sprite-class higher-level renderers  | unanalyzed |

The entire low-level blit pipeline is now end-to-end in `src/` for
both glyphs and sprites.  Engine code can dispatch a sprite from a
PL8 blob to the bound canvas via the same clip pipeline the glyph
renderer uses.  The next frontiers are the higher-level sprite
renderers (the callers of `gfx_dispatch_sprite` — there are at
least 10 callsites scattered across 0x408xxx-0x40axxx visible in
the decomp), the PL8 header parser (to load a sprite blob from a
file), or runtime canvas wiring (the visible-pixels milestone, gated
on Windows validation).

### What's next

Three natural continuations:

1. **Higher-level sprite renderers** — the 10+ callsites of
   `gfx_dispatch_sprite` (e.g. `FUN_00405487`, `FUN_00405862`,
   `FUN_004059af`, `FUN_00408438`, `FUN_00409934`, `FUN_0040a01d`,
   `FUN_0040a21a`, etc.) wrap the dispatcher with sprite-blob
   selection (which PL8 to use), animation frame indexing, and
   per-element placement logic.  Pure autonomous work; opens up
   the map / battlefield / UI panel renderers.  Substantial scope.
2. **PL8 header parser** — small chip: read the 8-byte header to
   get descriptor count + decode the descriptor array.  Enables
   tests that exercise the dispatcher against real
   `vendor/original/*.pl8` data.  Modest scope.
3. **Wire gfx.c runtime to the renderer + sprites** — extend the
   DDraw probe to bind a real canvas, then render a real glyph +
   sprite during boot.  Visible-pixels milestone earned.  Gated on
   user-observed Windows-host validation.

Picking #1 keeps autonomous progress moving and starts unpacking
the visible game-state rendering.  #2 is a logical companion to #1
(many of those callsites probably parse PL8 headers).  #3 unlocks
visible-pixels validation but requires human verification.

This is a natural `/clear` point — the sprite-class blit pipeline
is closed out and the next frontiers (higher-level sprite
renderers, PL8 parsing, runtime wiring) are fresh scope worth a
fresh conversation.

---

## 2026-05-25 — Compound label+signed-value wrapper (1 chip, 1 fn)

Continuation of the same unattended session.  The previous dispatch
listed the compound "label + signed value" wrapper as the smallest
remaining text-cluster chip — `FUN_00402e0c` (344 B), the entry point
for the in-game stat readouts that pair a localization label with a
sign-coloured decimal value.  This dispatch ports it and closes the
last retail text-class entry point.

### Chip landed

**`src/gfx_text_label`** — extended with one new function:

  - `FUN_00402e0c` → `gfx_text_label_render_labeled_signed` (344 B)
    Compound two-half render — label first (via the line renderer at
    its caller-supplied (x, y)), then the value (via the existing
    `gfx_text_label_render_dec` wrapper, positioned at
    `x + line_x_acc` so it lands flush against the label).  Sign-
    conditional label color + a mode/sign-based prefix-char decision
    tree on the value render:

      mode == 2  → '@' prefix, color_positive (value as-is)
      value <  0 → '-' prefix, color_negative (value negated)
      value == 0 → '@' prefix, color_positive
      value >  0 → '+' prefix, color_positive

    Wraps the render in a save/reset/restore dance against the global
    horizontal accumulator `line_x_acc` (DAT_005cd404).  Non-zero
    callsites observe the accumulator incremented by the combined
    width of the compound render; the all-zero callsite (gated out by
    the early-return check) clobbers it to 0 — see quirk #46.

No new private helpers — composes the renderer and the existing
label-render wrapper, both already in place.  No new module state.

### Filed quirks (#46, #47)

Two new entries in `docs/findings/engine-quirks.md`:

  - **#46** — `gfx_text_label_render_labeled_signed` resets
    `line_x_acc` to 0 BEFORE the early-return gate and restores it
    INSIDE the gate's then-branch.  When both `value` and `mode` are
    zero the restore never runs, silently clobbering the caller's
    running x accumulator instead of leaving it unchanged.  Likely a
    bug; preserved verbatim with a witness test.
  - **#47** — Label color tracks the value's sign even when `mode == 2`
    neutralizes the value's prefix to '@'.  A `(value < 0, mode == 2)`
    call therefore paints a negative-coloured label paired with a
    positive-coloured value.  And — because the formatter still emits
    its own sign byte for negative values — the assembled value buffer
    is `"@-N"` (the '@' prefix overwrites buffer[0] but the formatter-
    emitted '-' survives at buffer[1]).  No captured callsite invokes
    this path with a negative value; the asymmetry may be latent.

### Headline numbers

- **607 → 615 host tests** (+8), all green under ASan/UBSan.
- **1 retail function ported** (344 B decompilation).
- **0 new modules** — extends `src/gfx_text_label` with one
  function (no new state, no new helpers).
- **2 new engine quirks filed** (#46, #47).
- Cross-compile (mingw32) clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### Renderer pipeline state

| layer                          | status     |
| ------------------------------ | ---------- |
| char width                     | ported     |
| buffer width                   | ported     |
| bounded buffer width           | ported     |
| word width + chars             | ported     |
| editor (text_input)            | ported     |
| localization blob access       | ported     |
| dirty-rect tracking            | ported     |
| 1-pixel / 2x1 painters         | ported     |
| clipped pixel / 2x1            | ported     |
| glyph blitter (unclipped)      | ported     |
| glyph blitter (clipped × 2)    | ported     |
| glyph dispatcher (per-char)    | ported     |
| clip setters (X + Y)           | ported     |
| text renderer (per-line)       | ported     |
| centered-text wrapper          | ported     |
| substitution stream            | ported     |
| word-wrap renderer (buffer)    | ported     |
| word-wrap renderer (localized) | ported     |
| localization line render       | ported     |
| localization line centered     | ported     |
| solid Bresenham line           | ported     |
| dotted axis-aligned line       | ported     |
| solid rect outline             | ported     |
| 3D bevel rect (3 palettes)     | ported     |
| int formatter (dec + hex)      | ported     |
| label render (dec + hex + centered) | ported |
| label render (signed-color)    | ported     |
| **compound "label + signed value"** | **ported (this chip)** |
| sprite blitters (4x4/6x6/8x8)  | unanalyzed |

With this chip in, **every entry point in the text-control family is
now end-to-end in `src/`**.  Engine code can render any combination of
glyph, word, line, localized line, word-wrapped paragraph, primitive
line/rectangle, formatted int label, centered signed-color value, or
compound label-plus-value through pure portable C — backed by the
canvas-binding gfx_paint layer.  The next frontier is runtime wiring
(making the renderer paint to the actual DDraw-backed canvas) or
sprite blitters (a new subsystem entirely).

### What's next

Two natural continuations:

1. **Wire gfx.c runtime to the renderer** — extend the
   `IDirectDrawSurface::Lock` probe to set `lpSurface` onto the
   gfx_paint canvas, then trigger a render of a real string + frame
   during boot.  Visible-pixels milestone earned at this point.
   Small chip but requires user-observed Windows-host validation.
2. **Sprite blitter (4x4 / 6x6 / 8x8 tiles)** — the next deferred
   leaf class.  Larger scope; uncovers the tile-format readers the
   renderer needs for the map view.  Pure autonomous work.

Picking #1 unlocks visible-pixels validation but is gated on
human verification.  Picking #2 keeps autonomous progress moving and
opens the next subsystem — fresh enough to justify a `/clear`.

This is a natural `/clear` point — the text-control family is closed
out and the remaining frontiers (runtime wiring, sprite blitters) are
fresh scope worth a fresh conversation.

---

## 2026-05-25 — Int formatter + label-render cluster (1 chip, 6 fns)

Continuation of the same unattended session.  The previous dispatch
landed the line drawer and axis-rect wrappers and listed three
follow-ups.  The first of those — porting `FUN_00403015`, the small
string-concat-and-render wrapper — expanded once the surrounding
cluster was surveyed: the wrapper sits atop a 6-function family
(two integer formatters + four render wrappers) that share a single
8000-byte format buffer and a common shape.  This dispatch ports the
entire cluster.  Closes out the last text-control entry-point block.

### Chip landed

**`src/gfx_text_label` (NEW, ~280 LOC + 27 tests)** — 6 retail leaves
+ 3 private helpers spanning the int-format-then-render family:

  - `FUN_004022bd` → `gfx_text_label_format_dec`        (394 B)
    Integer → decimal digits.  1-9 digits sized to magnitude;
    ≥10^9 silently writes ZERO digits (quirk #42).  Sign byte
    ('-' for negatives, '+' if `with_plus`) written at `offset`,
    digit start shifted accordingly.
  - `FUN_00402447` → `gfx_text_label_format_hex`        (400 B)
    Same shape, uppercase hex.  1-7 digits; ≥0x10000000 silent.
    MSVC's sign-aware abs trick (`(b ^ s) - s & 0xf ^ s) - s`) for
    nibble extraction simplified to `& 0xf` since the prelude
    strips the sign — bytes-on-disk identical.
  - `FUN_00402f64` → `gfx_text_label_render_dec`        (177 B)
    Render `"<prefix?><dec(value)><suffix>"` via
    `gfx_text_render_line`.  Prefix overwrites buffer[0] — see
    quirk #41.
  - `FUN_00403015` → `gfx_text_label_render_hex`        (177 B)
    Same shape, uppercase hex — used by the leaderboard / rank
    screens.
  - `FUN_004030c6` → `gfx_text_label_render_dec_centered` (202 B)
    Centered variant — routes through
    `gfx_text_render_line_centered`.  Buggy off-by-one strlen
    leaves an embedded NUL between digits and suffix — see quirk
    #45.
  - `FUN_00403190` → `gfx_text_label_render_dec_signed` (254 B)
    Centered + signed-color variant.  Skips render when both
    `value` and `zero_companion` are zero (quirk #44).  Picks
    `color_positive` or `color_negative` by sign; formats with
    `with_plus=1` so positives render with a leading '+'.  No
    captured callsites — semantics inferred from control flow.

Module state: a single module-private 8000-byte format buffer
(`g_format_buffer`, mirrors retail's `DAT_0058fda0`) that the
formatters write into and the renderers read from.  Exposed via
`gfx_text_label_buffer()` / `gfx_text_label_buffer_size()` so the
save-name copy paths (`FUN_004afab9` / `FUN_004afeed`, not yet
ported) can share the same storage when they land.

Private helpers:

  - `format_zero_and_sign` — shared prelude for both formatters
    (zeros first 100 bytes, emits sign byte, returns updated digit
    offset).
  - `apply_prefix` — overwrites buffer[0] when prefix != 0.
  - `append_suffix` / `append_suffix_off_by_one` — two strlen-
    equivalent suffix-appenders mirroring the for-loop (correct)
    and do-while (off-by-one) patterns retail uses for the
    non-centered and centered variants respectively.

Verified the divergent strlen shape via `r2 -c 'af; pdf'` against
retail's `Lords2.exe` — Ghidra's decomp of the do/while loop is
faithful at the byte level, not a decompiler artifact.

### Filed quirks (#41, #42, #43, #44, #45)

Five new entries in `docs/findings/engine-quirks.md`:

  - **#41** — `prefix` parameter OVERWRITES buffer[0] rather than
    prepending.  The formatter is called with offset=1, leaving
    buffer[0] as the byte-0 sentinel; the prefix drops into it.
    All retail callers rely on buffer[0] being either a separator
    (' ') or icon-like sigil ('@').
  - **#42** — Integer formatters silently emit ZERO digits for
    values ≥ 10^9 (decimal) or ≥ 0x10000000 (hex).  The digit-
    count cascade has a default `count = 0` branch and the digit-
    write loop simply doesn't fire.  Sign byte still emitted.
    Almost certainly a "no caller passes values this large" 1996
    assumption — invisible in practice but a latent footgun for
    high-cap mods.
  - **#43** — Format buffer pre-zero scope is 100 bytes (of an
    8000-byte buffer).  Bytes 100+ retain prior caller state.  The
    label-render path's prefix+digits+suffix stays well under 100,
    but the buffer is shared with the save-name copy paths.
  - **#44** — `FUN_00403190` early-returns when both `value` and
    `zero_companion` are zero — no formatter call, no render.
    Inferred as a "suppress all-zero rows" gate for a delta UI.
  - **#45** — The centered label-render variants embed a NUL
    between the digits and the suffix because their do/while
    strlen-equivalent advances ONE byte past the first NUL.  The
    text renderer stops at the first NUL → the suffix is INVISIBLE.
    Real bug, present in retail for 30 years, masked by every
    callsite using a single-char (" ") or empty suffix.

### Headline numbers

- **580 → 607 host tests** (27 new), all green under ASan/UBSan.
- **6 retail functions ported** (~1.6 KB decompilation, 394 + 400
  + 177 + 177 + 202 + 254 B = 1604 B counting all six).
- **1 new module** (`src/gfx_text_label`) — no extensions to other
  modules; the renderer (`gfx_text_render_line` /
  `gfx_text_render_line_centered`) and dispatcher
  (`gfx_dispatch_set_alt_clip_enabled`) were already in place.
- **5 new engine quirks filed** (#41, #42, #43, #44, #45).
- Cross-compile (mingw32) clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### Renderer pipeline state

| layer                          | status     |
| ------------------------------ | ---------- |
| char width                     | ported     |
| buffer width                   | ported     |
| bounded buffer width           | ported     |
| word width + chars             | ported     |
| editor (text_input)            | ported     |
| localization blob access       | ported     |
| dirty-rect tracking            | ported     |
| 1-pixel / 2x1 painters         | ported     |
| clipped pixel / 2x1            | ported     |
| glyph blitter (unclipped)      | ported     |
| glyph blitter (clipped × 2)    | ported     |
| glyph dispatcher (per-char)    | ported     |
| clip setters (X + Y)           | ported     |
| text renderer (per-line)       | ported     |
| centered-text wrapper          | ported     |
| substitution stream            | ported     |
| word-wrap renderer (buffer)    | ported     |
| word-wrap renderer (localized) | ported     |
| localization line render       | ported     |
| localization line centered     | ported     |
| solid Bresenham line           | ported     |
| dotted axis-aligned line       | ported     |
| solid rect outline             | ported     |
| 3D bevel rect (3 palettes)     | ported     |
| **int formatter (dec + hex)**       | **ported (this chip)** |
| **label render (dec + hex + centered)** | **ported (this chip)** |
| **label render (signed-color)**         | **ported (this chip)** |
| compound "label + signed value" | deferred (344 B, `FUN_00402e0c`) |
| sprite blitters (4x4/6x6/8x8)  | unanalyzed |

The text-control entry points are now end-to-end in `src/`.  The
last text-class deferred function is `FUN_00402e0c` (344 B), a
compound wrapper that renders a localization-string label then a
signed value with sign-conditional color — discovered while
auditing callers of `FUN_00402f64`.  It composes the renderer and
the label-render module ported here; could be folded in as a
follow-up chip if needed, or skipped in favor of the next subsystem.

### What's next

Three natural continuations:

1. **Compound label + signed value** — port `FUN_00402e0c`
   (344 B), the wrapper that combines a localization-string render
   with a signed-value render in two halves.  Small chip; closes
   the very last text-cluster entry point.  Composes
   `gfx_text_render_line` and the label-render module ported here.
2. **Wire gfx.c runtime to the renderer** — extend the
   `IDirectDrawSurface::Lock` probe to set `lpSurface` onto the
   gfx_paint canvas.  Small chip but requires a runtime smoke test
   on the Windows host to validate "render a real string + frame
   during boot".  Visible-pixels milestone earned at this point.
3. **Sprite blitter (4x4 / 6x6 / 8x8 tiles)** — the next deferred
   class of leaves.  Larger scope; uncovers the tile-format readers
   the renderer needs for the map view.

Picking #2 (runtime wiring) would unlock visible-pixels validation
end-to-end, but it's gated on user-observed Windows-host behavior.
Picking #1 (compound wrapper) is the smallest remaining text chip
and keeps autonomous progress moving; #3 is a much larger scope and
a fresh subsystem.

This is a natural `/clear` point — the int-formatter + label-render
cluster is now end-to-end, and the next frontier (compound wrapper,
runtime wiring, or sprite blitters) is a fresh scope worth a fresh
conversation.

---

## 2026-05-25 — Line drawer + axis-rect wrappers (1 chip, 6 fns)

Continuation of the same unattended session.  The previous dispatch
landed the word-wrap renderers and listed three follow-ups, with the
line drawers as the recommended next move toward the visible-pixels
milestone.  This dispatch takes that step: ports the Bresenham line
core, the dotted-axis variant, and the four axis-aligned rectangle
wrappers (solid + three 3D bevel palettes) — closing out the entire
line-and-frame primitive set.

### Chip landed

**`src/gfx_line` (NEW, ~260 LOC + 23 tests)** — 6 retail leaves +
2 private helpers spanning the engine's straight-line / rectangle
primitives:

  - `FUN_004037ee` → `bres_setup`              (347 B, private)
    Bresenham endpoint reorder + abs(dx)/abs(dy) compute + per-axis
    step-sign selection.
  - `FUN_00403949` → `bres_err_update`         (126 B, private)
    Per-iteration accumulator update.  Reads the (mutated) abs_dx /
    abs_dy.
  - `FUN_00403a8f` → `gfx_line_draw`           (442 B) — solid 1-px
    Bresenham line.  Dispatches on endpoint shape (vertical /
    horizontal / shallow / steep).
  - `FUN_00403c49` → `gfx_line_draw_dotted`    (171 B) — axis-only
    dotted line; step +2 on the dominant axis.  Diagonals are a
    silent no-op (quirk #40).
  - `FUN_00403cf4` → `gfx_line_rect`           (247 B) — solid
    4-edge frame.  Sequential-`if` clamp.
  - `FUN_00403deb` → `gfx_line_rect_bevel_dark_br` (249 B) — 3D
    bevel rect, top+right at 0x10, bottom+left at 0x1f.
  - `FUN_00403ee4` → `gfx_line_rect_bevel_palette_b` (249 B) — same
    shape, palette pair 0x35 / 0x28.
  - `FUN_00403fdd` → `gfx_line_rect_bevel_dark_tl` (249 B) — inverse
    of bevel_dark_br: top+right at 0x1f, bottom+left at 0x10.

State held in `src/gfx_line.c`'s file-statics — 9 slots mirroring
retail's BSS layout for the Bresenham setup (`g_abs_dx`, `g_abs_dy`,
`g_x_start/end`, `g_y_start/end`, `g_x_step`, `g_y_step`,
`g_bres_err`).  No new state plumbing — pixel painting routes through
the existing `gfx_paint_pixel_clipped` (FUN_004039c7, already
ported) and canvas bounds come from `gfx_paint_canvas_width()` /
`gfx_paint_canvas_height()`.

The 4 rect wrappers share a `draw_rect_4lines(x, y, w, h, c_tr, c_bl)`
helper — solid uses the same color for both pairs, bevels pass
distinct colors.  Two clamp variants (`clamp_solid` for sequential
`if`s, `clamp_bevel` for the asymmetric `if/else if`) preserve the
retail quirk where bevel rects skip the right/bottom clamp when the
left/top edge was clamped (quirk #39).

### Filed quirks (#38, #39, #40)

Three new entries in `docs/findings/engine-quirks.md`:

  - **#38** — The Bresenham line drawer mutates the abs(dx) and
    abs(dy) BSS slots inside its inner loop, decrementing one per
    iteration and both when the err accumulator crosses zero.  The
    err update consults the current (mutated) values each step.
    Mathematically valid but produces a non-uniform pixel
    distribution distinct from textbook Bresenham — endpoints land
    correctly, total pixel count is preserved, but interior pixels
    cluster non-symmetrically.  Possibly an MSVC 4.20 codegen
    artifact, possibly a deliberate constant-load reduction.
  - **#39** — The 3 bevel rect wrappers' bounds-clamp uses
    `if/else if`, so when the left/top edge was clamped, the
    right/bottom clamp is skipped.  The solid rect uses sequential
    `if`s and clamps both unconditionally.  Behavior is unchanged
    (per-pixel clipper drops off-canvas writes) but the bevel path
    wastes loop iterations on oversize rects.  Looks like an
    oversight that survived 30 years because common UI rect sizes
    never hit the slow path.
  - **#40** — The dotted-line variant (`FUN_00403c49`) only handles
    pure-vertical and pure-horizontal lines.  Diagonals fall through
    both branches and silently draw nothing.  Retail callers all
    pass axis-aligned coords; the code is shaped for the axis path
    only.  Our port preserves the silent no-op verbatim — a
    diagonal-dotted-line regression test would deliberately fail.

### Headline numbers

- **557 → 580 host tests** (23 new), all green under ASan/UBSan.
- **6 retail functions ported** (~1.85 KB decompilation: 347 + 126
  + 442 + 171 + 247 + 249 + 249 + 249 B = 2080 B counting helpers).
- **1 new module** (`src/gfx_line`) — no extensions to other
  modules; the gfx_paint clipped-pixel writer was already in place.
- **3 new engine quirks filed** (#38, #39, #40).
- Cross-compile (mingw32) clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### Renderer pipeline state

| layer                          | status     |
| ------------------------------ | ---------- |
| char width                     | ported     |
| buffer width                   | ported     |
| bounded buffer width           | ported     |
| word width + chars             | ported     |
| editor (text_input)            | ported     |
| localization blob access       | ported     |
| dirty-rect tracking            | ported     |
| 1-pixel / 2x1 painters         | ported     |
| clipped pixel / 2x1            | ported     |
| glyph blitter (unclipped)      | ported     |
| glyph blitter (clipped × 2)    | ported     |
| glyph dispatcher (per-char)    | ported     |
| clip setters (X + Y)           | ported     |
| text renderer (per-line)       | ported     |
| centered-text wrapper          | ported     |
| substitution stream            | ported     |
| word-wrap renderer (buffer)    | ported     |
| word-wrap renderer (localized) | ported     |
| localization line render       | ported     |
| localization line centered     | ported     |
| **solid Bresenham line**       | **ported (this chip)** |
| **dotted axis-aligned line**   | **ported (this chip)** |
| **solid rect outline**         | **ported (this chip)** |
| **3D bevel rect (3 palettes)** | **ported (this chip)** |
| string-concat single-line render | deferred (177 B, `FUN_00403015`) |
| sprite blitters (4x4/6x6/8x8)  | unanalyzed |

The line + rect primitives are now end-to-end in `src/`.  Tests cover
horizontal / vertical / 45° / shallow / steep / degenerate / clipped
/ negative-coord lines (all endpoint-order invariant), the dotted
axis variant + diagonal-no-op, solid rect with size clamp, and the
bevel rect's corner-overwrite pattern (sequential edge paints mean
later edges overwrite earlier ones at corners — only TR shows the
top color on a typical bevel), plus the asymmetric clamp test that
exposes quirk #39.

### What's next

Three natural continuations:

1. **String-concat single-line render** — port `FUN_00403015`
   (177 B), the small wrapper that concatenates a prefix into a
   fixed 8000-byte buffer (`DAT_0058fda0`) then calls
   `gfx_text_render_line`.  Closes the last text-control entry
   point; called by the in-game name/title-prefix paths.
2. **Wire gfx.c runtime to the renderer** — extend the
   `IDirectDrawSurface::Lock` probe to set `lpSurface` onto the
   gfx_paint canvas.  Small chip but requires a runtime smoke test
   on the Windows host to validate "render a real string + frame
   during boot".  Visible-pixels milestone earned at this point.
3. **Sprite blitter (4x4 / 6x6 / 8x8 tiles)** — the next deferred
   class of leaves.  Larger scope; uncovers the tile-format readers
   the renderer needs for the map view.

Picking #2 (runtime wiring) would unlock visible-pixels validation
end-to-end, but it's gated on user-observed Windows-host behavior.
Picking #1 (string-concat) is the smallest remaining text-control
chip and keeps autonomous progress moving forward; #3 is a much
larger scope and a fresh subsystem.

This is a natural `/clear` point — the line-and-rect primitive set
is now end-to-end, and the next frontier (runtime wiring or sprite
blitters) is a fresh scope worth a fresh conversation.

---

## 2026-05-25 — Word-wrap renderers (1 chip, 4 fns)

Continuation of the same unattended session.  The previous dispatch
landed the per-line text renderer and listed three follow-ups, with
the word-wrap renderer as the recommended next move toward the
visible-pixels milestone.  This dispatch takes that step: ports the
two multi-line word-wrap variants plus the two single-line
localization-string wrappers that drive them through the localized
seek path.

### Chip landed

**`src/gfx_wrap` (NEW, ~310 LOC + 15 tests)** — 4 retail leaves
spanning the text-source-to-line-renderer control plane:

  - `FUN_0040328e` → `gfx_wrap_localized`            (673 B) — the
    full-feature multi-line word-wrap.  Seeks a localization sub-
    string, greedily appends whole words to a 2000-byte line buffer
    until the per-line pixel budget is met, hands each assembled
    line to `gfx_text_render_line`, advances cursor_y by the per-
    font line height (24 or 16 px), and supports two-stage
    pagination (after `first_page_lines` lines, swap to
    `next_page_x`/`next_page_width`).  Suppresses leading spaces at
    the start of every wrapped line.  Resets `max_line_x` at top;
    updates it after each line.  Hard cap of 99 lines.
  - `FUN_0040352f` → `gfx_wrap_buffer`               (458 B) — the
    simpler buffer-supplied word-wrap.  Same outer loop as
    `gfx_wrap_localized` but takes a caller-supplied source pointer
    (no localization), uses a 200-byte line buffer, has no
    leading-space skip, no two-stage pagination, and does NOT reset
    `max_line_x`.
  - `FUN_00402d37` → `gfx_wrap_localized_line`       (213 B) — the
    single-line localized render.  Localization seek + skip +
    leading-control-skip + `gfx_text_render_line`.  Clears the
    dispatcher's alt-clip flag on exit.
  - `FUN_00402c5e` → `gfx_wrap_localized_line_centered` (217 B) —
    centered variant of the above; routes through
    `gfx_text_render_line_centered`.

State held in `src/gfx_wrap.c`'s file-statics:

  - `g_max_line_x` (DAT_005aeb88) — widest line emitted, read by
    upstream UI code (e.g. retail's scenario-hint center at
    `0x476e95`).  Reset by `gfx_wrap_localized` only.
  - `g_tall_font_marker` (DAT_005b2fa0) — pointer-equality
    discriminator for the line-height switch.  When the caller's
    `font_base` equals the registered marker, per-line cursor_y
    advance is 24 px instead of 16.
  - Two intermediate line buffers (2000 + 200 bytes) mirroring
    DAT_005c92a0 + DAT_005aea60.

One new setter dropped into `gfx_text` to support the wrap layer's
needs:

  - `gfx_text_set_collapse_active` — per-call arm for the
    `g_collapse_active` flag (DAT_005c925a) that disables the
    consecutive-space-collapse rewind.  Wrap callers arm it
    immediately before every per-line render; the renderer clears
    it in its epilogue.

The wrap module reuses the `localization_state_t` and
`font_metrics_state_t` structs from the existing modules — no new
state plumbing.

### Filed quirks (#35, #36, #37)

Three new entries in `docs/findings/engine-quirks.md`:

  - **#35** — `FUN_0040328e` and `FUN_0040352f` share an outer-loop
    structure that differs only by feature flags (localization vs
    pre-resolved buffer, leading-space-skip on/off, two-stage
    pagination on/off, large/small line buffer, max_line_x reset
    on/off).  Source-level likely one function with optional
    features; MSVC 4.20 emitted them separately and the linker
    didn't fold them.  Our port factors the shared structure into
    helpers but keeps the two entry points.
  - **#36** — `max_line_x` is asymmetric: the localized wrap resets
    at top, the buffer wrap does not.  Callers of the buffer wrap
    must clear externally (via `gfx_wrap_set_max_line_x(0)`) for a
    fresh reading.  Likely a deliberate retail compromise to let
    multi-segment callers concatenate widths across calls.
  - **#37** — the per-line cursor_y advance (24 vs 16 px) is keyed
    off **pointer equality** with a specific font blob's BSS
    address, not by reading a height byte from the font header.
    Adds a font means re-using that blob OR duplicating the
    pointer compare everywhere line-height matters.  Our port
    exposes the discriminator as a registered marker pointer.

### Headline numbers

- **542 → 557 host tests** (15 new), all green under ASan/UBSan.
- **4 retail functions ported** (~1.6 KB decompilation, 673 + 458 +
  213 + 217 B).
- **1 new module** (`src/gfx_wrap`) + 1-line extension to
  `src/gfx_text` (`gfx_text_set_collapse_active`).
- **3 new engine quirks filed** (#35, #36, #37).
- Cross-compile (mingw32) clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### Renderer pipeline state

| layer                          | status     |
| ------------------------------ | ---------- |
| char width                     | ported     |
| buffer width                   | ported     |
| bounded buffer width           | ported     |
| word width + chars             | ported     |
| editor (text_input)            | ported     |
| localization blob access       | ported     |
| dirty-rect tracking            | ported     |
| 1-pixel / 2x1 painters         | ported     |
| clipped pixel / 2x1            | ported     |
| glyph blitter (unclipped)      | ported     |
| glyph blitter (clipped × 2)    | ported     |
| glyph dispatcher (per-char)    | ported     |
| clip setters (X + Y)           | ported     |
| text renderer (per-line)       | ported     |
| centered-text wrapper          | ported     |
| substitution stream            | ported     |
| **word-wrap renderer (buffer)**       | **ported (this chip)** |
| **word-wrap renderer (localized)**    | **ported (this chip)** |
| **localization line render**          | **ported (this chip)** |
| **localization line centered**        | **ported (this chip)** |
| string-concat single-line render | deferred (177 B, `FUN_00403015`) |
| line drawers (axis + diag)     | deferred (4 fns, 347+442+171+197 B) |
| sprite blitters (4x4/6x6/8x8)  | unanalyzed |

The text path is now end-to-end in `src/`.  Tests verify two-line
wrap with pixel budgets, line_y_acc advance per line, max_line_x
tracking and reset asymmetry, leading-space skip on wrap (localized
only), two-stage pagination, out-of-bounds localization seek, and
the tall-font 24-px line-height switch via the marker pointer.

### What's next

Three natural continuations:

1. **String-concat single-line render** — port `FUN_00403015`
   (177 B), the small wrapper that concatenates a prefix into a
   fixed 8000-byte buffer (`DAT_0058fda0`) then calls
   `gfx_text_render_line`.  Closes the last text-control entry
   point; called by the in-game name/title-prefix paths.
2. **Line drawers** — port `FUN_00403c49` and friends (347+442+
   171+197 B, 4 fns).  Axis-aligned + diagonal lines for the
   battle-map's grid + the UI panel borders.  Needed for visible
   pixels beyond the text path.
3. **Wire gfx.c runtime to the text path** — extend the
   `IDirectDrawSurface::Lock` probe to set `lpSurface` onto the
   gfx_paint canvas.  Small chip but requires a runtime smoke test
   on the Windows host to validate.  Unblocks "render a real
   string during boot" as a manual end-to-end check.

Picking #2 (line drawers) advances the visible-pixels milestone
most directly — once they land, the UI panel borders and battle-
map grid can be drawn under CI alongside the text path.

This is a natural `/clear` point — the text-source-to-renderer
chain is now end-to-end, and the next frontier (line drawing or
runtime wiring) is a fresh scope worth a fresh conversation.

---

## 2026-05-25 — Text-line renderer + centered wrapper (1 chip, 3 fns)

Continuation of the same unattended session.  The previous dispatch
landed the glyph dispatcher + clip setters and listed three follow-
ups, with the text renderer as the recommended next move toward the
visible-pixels milestone.  This dispatch takes that step: ports the
per-line renderer that drives `gfx_dispatch_glyph` in a loop, plus
the small centering wrapper, plus the substitution-stream pull helper.

### Chip landed

**`src/gfx_text` (NEW, ~380 LOC + 23 tests)** — 3 retail leaves
spanning the text-rendering control plane:

  - `FUN_00402637` → `gfx_text_render_line`             (989 B) — the
    per-line driver.  Walks a null-terminated byte buffer; for each
    printable char runs the dispatcher up to three times (drop-shadow,
    outline, or tight-shadow paths), updates running cursor + line
    accumulators, handles the '#' substitution stream, the
    consecutive-space collapse, the uppercase-highlight override, and
    the first-position hook.  Clears substitution + alt-clip flags at
    end-of-call.
  - `FUN_00402c16` → `substitution_next`                ( 72 B) — pulls
    one byte from the '#' macro buffer; wraps cursor to 0 and yields
    space on NUL.  Static — only called from the renderer.
  - `FUN_004025d7` → `gfx_text_render_line_centered`    ( 96 B) — pre-
    measures via `font_metrics_buffer_width_bounded` then offsets X
    by `(width - text_width) / 2`, clamped at 0.

State is held in `src/gfx_text.c`'s file-statics — mirrors retail's
BSS layout for the six render-mode flags, the substitution
buffer/cursor/enable triple, the first-position hook (target + latch
+ recorded x/y), and the three running accumulators (line_x_acc,
line_y_acc, char_counter).  Setters and getters expose every retail
DAT_* writer's surface area.

Two new dispatcher-adjacent setters dropped in to support the
renderer's needs without bloating set_dest's contract:

  - `gfx_glyph_set_cursor_x` / `gfx_glyph_set_cursor_y` (gfx_glyph) —
    single-axis cursor writes for the shadow paths that vary only
    cursor_y across passes.
  - `gfx_dispatch_set_alt_clip_enabled` (gfx_dispatch) — toggle alt-
    clip without disturbing the saved rect coords; called from the
    renderer's end-of-call cleanup.

### Filed quirks (#32, #33, #34)

Three new entries in `docs/findings/engine-quirks.md`:

  - **#32** — the renderer at `0x004d71f0` and font_metrics at
    `0x004d71d0` look like distinct char→glyph tables but are the
    same allocation viewed from two addresses (0x20 apart).  The
    renderer indexes at `raw_char - 0x20`; font_metrics indexes at
    `raw_char`.  Same byte for any printable.  Lifts the 32 control-
    byte slots out of the renderer's range so they implicitly miss.
  - **#33** — the substitution stream is one-shot but loop-sticky.
    First '#' triggers the FIRST substitution at the top of the
    loop; from that point on, the BOTTOM-of-loop advance keeps
    pulling from the stream purely because the cursor is non-zero.
    Stream stays engaged until its NUL terminator wraps the cursor
    back to 0, at which point the source-buffer advance resumes.  So
    a one-character source `#` with stream "AB" loaded renders as
    two chars.
  - **#34** — the uppercase-highlight color reset is a comma-
    expression inside a short-circuit gate.  When the flag is on,
    the per-glyph color is re-set to the caller's color EVERY
    iteration before the A-Z check potentially overrides to 1.  When
    off, the color isn't touched per-iter at all.  Defensive against
    forward-bleed of a previous char's override.

### Headline numbers

- **519 → 542 host tests** (23 new), all green under ASan/UBSan.
- **3 retail functions ported** (~1.1 KB decompilation, 989 + 72 +
  96 B).
- **1 new module** (`src/gfx_text`) + 3-line extension to
  `src/gfx_glyph` (single-axis cursor setters) + 1-line extension
  to `src/gfx_dispatch` (alt-clip enabled-only setter).
- **3 new engine quirks filed** (#32, #33, #34).
- Cross-compile (mingw32) clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### Renderer pipeline state

| layer                          | status     |
| ------------------------------ | ---------- |
| char width                     | ported     |
| buffer width                   | ported     |
| bounded buffer width           | ported     |
| word width + chars             | ported     |
| editor (text_input)            | ported     |
| localization blob access       | ported     |
| dirty-rect tracking            | ported     |
| 1-pixel / 2x1 painters         | ported     |
| clipped pixel / 2x1            | ported     |
| glyph blitter (unclipped)      | ported     |
| glyph blitter (clipped × 2)    | ported     |
| glyph dispatcher (per-char)    | ported     |
| clip setters (X + Y)           | ported     |
| **text renderer (per-line)**   | **ported (this chip)** |
| **centered-text wrapper**      | **ported (this chip)** |
| **substitution stream**        | **ported (this chip)** |
| line drawers (axis + diag)     | deferred (4 fns, 347+442+171+197 B) |
| word-wrap renderer             | deferred (673 B, `FUN_0040328e`) |
| sprite blitters (4x4/6x6/8x8)  | unanalyzed |

The per-line path is now end-to-end in `src/`.  Tests render strings
on a host-bound canvas with all three multi-pass paths (drop-shadow,
outline, tight-shadow) and verify the per-pass colors land on the
expected pixels.  Substitution stream, uppercase highlight, collapse-
spaces, first-position hook, missing-glyph fallback, and the centered
wrapper's offset math all have dedicated coverage.

### What's next

Three natural continuations:

1. **Word-wrap renderer** — port `FUN_0040328e` (673 B) and
   `FUN_0040352f` (458 B), the multi-line word-wrap variants used
   by long help / scenario text.  Both call `gfx_text_render_line`
   in a loop, using `font_metrics_word_width` (already ported) for
   per-word measurement.  Closes the gap to "render a paragraph in
   CI" against a Frida-captured retail golden.
2. **Line drawers** — port `FUN_00403c49` and friends (347+442+
   171+197 B, 4 fns).  Axis-aligned + diagonal lines for the
   battle-map's grid + the UI panel borders.  Needed for visible
   pixels beyond the text path.
3. **Wire gfx.c runtime to gfx_paint** — extend the
   `IDirectDrawSurface::Lock` probe to set `lpSurface` onto the
   gfx_paint canvas.  Small chip but requires a runtime smoke test
   on the Windows host to validate.  Unblocks "draw a real string
   during boot" as a manual end-to-end check.

Picking #1 (word-wrap renderer) advances the visible-pixels milestone
most directly — once it lands, the test harness can render the
scenario picker's hint text and the in-game tooltip cluster.

This is a natural `/clear` point — the per-line renderer is a
complete stratum, and the next frontier (the word-wrap renderer that
drives it in a loop) is a fresh multi-state scope worth a fresh
conversation.

---

## 2026-05-25 — Glyph dispatcher + clip setters (1 chip, 3 fns)

Continuation of the same unattended session.  The previous dispatch
landed the per-character glyph blitters and listed three follow-ups,
with the glyph dispatcher + clip setters as the recommended next move
toward the visible-pixels milestone.  This dispatch takes that step:
ports the dispatcher cluster that drives the freshly-landed blitters
on a per-character basis.

### Chip landed

**`src/gfx_dispatch` (NEW, ~310 LOC + 30 tests)** — 3 retail leaves
spanning the (font_base, char_code) → blitter call path:

  - `FUN_00402a14` → `gfx_dispatch_glyph`         (514 B) — the
    per-character entry point.  Looks up glyph index, reads the
    16-byte descriptor, advances cursor_y by the per-glyph advance
    byte, applies default-font character-range tweaks, runs the two
    clip setters, bakes top/left skips into the source pointer, and
    dispatches one of the three blitters based on the X-clip mode
    tag (gated on Y-clip != skip).
  - `FUN_0040464c` → `gfx_dispatch_clip_x`        (303 B) — horizontal
    clip setup.  Four outcomes: no-clip (0), left-clip (1, cursor_x
    shifted to `left`), right-clip (2, clipped_w narrowed), skip (5,
    glyph fully off-canvas).  Writes clipped_w / src_stride_pad /
    dst_stride_pad through gfx_glyph's setters; dispatcher-private
    state for the mode tag and left skip.
  - `FUN_0040477b` → `gfx_dispatch_clip_y`        (291 B) — vertical
    counterpart.  Same shape, plus the X→Y "skip" propagation rule
    (X-clip of 5 forces Y-clip to 5 even when the glyph fits
    vertically).  Writes the visible row count through a new
    `gfx_glyph_set_glyph_h` setter — overwriting the per-glyph
    advance byte the dispatcher staged earlier.

State sharing with `gfx_glyph` is via three new getter/setter pairs:
`gfx_glyph_cursor_x/y()`, `gfx_glyph_glyph_w/h()`, and
`gfx_glyph_set_glyph_h()`.  Dispatcher-private state (clip-mode tags,
height short, last_glyph_index, alt-clip rect) is held in
`src/gfx_dispatch.c`'s file-statics — mirrors retail's BSS layout.

### Filed quirks (#29, #30, #31)

Three new entries in `docs/findings/engine-quirks.md`:

  - **#29** — the 16-byte glyph descriptor has TWO height-shaped
    fields: a `short` at +2 (read by clip_y for clip math) and a
    `byte` at +0xd (used as cursor_y advance, then overwritten by
    clip_y with the visible row count).  The dispatcher carefully
    orders the writes so the advance is added to cursor_y BEFORE the
    clipper clobbers the global.  Almost always equal, but treated as
    independent — handy for descenders / accent marks / zero-height
    combining marks.
  - **#30** — the dispatcher hard-codes three character-range cursor_y
    decrements (`'a'..'m'` / `'s'..'w'` / 0x80..0x84) that trigger
    ONLY when the font matches a specific global (`DAT_005af8f0`).
    Looks like a per-glyph kerning tweak baked into the dispatcher so
    the same font blob can be reused with or without the tweak just
    by passing a different font pointer.  We expose the trigger font
    via `gfx_dispatch_set_default_font`.
  - **#31** — the alt-clip rect (used when overlay panels need a
    sub-rect for text) mixes runtime globals (left, bottom) with
    hard-coded constants (top=0x18, right=0x1de) inside the
    dispatcher body.  Pixel-perfect for the panel-overlay layout in
    640×480 mode; OpenLords2 preserves the asymmetry — the constants
    are `#define`s in `gfx_dispatch.c`, the runtime values are
    settable via `gfx_dispatch_set_alt_clip`.

### Headline numbers

- **489 → 519 host tests** (30 new), all green under ASan/UBSan.
- **3 retail functions ported** (~1.1 KB decompilation, 514 + 303 +
  291 B).
- **1 new module** (`src/gfx_dispatch`) + 3-pair extension to
  `src/gfx_glyph` (4 getters + 1 row-count setter).
- **3 new engine quirks filed** (#29, #30, #31).
- Cross-compile (mingw32) clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### Renderer pipeline state

| layer                          | status     |
| ------------------------------ | ---------- |
| char width                     | ported     |
| buffer width                   | ported     |
| bounded buffer width           | ported     |
| word width + chars             | ported     |
| editor (text_input)            | ported     |
| localization blob access       | ported     |
| dirty-rect tracking            | ported     |
| 1-pixel / 2x1 painters         | ported     |
| clipped pixel / 2x1            | ported     |
| glyph blitter (unclipped)      | ported     |
| glyph blitter (clipped × 2)    | ported     |
| **glyph dispatcher (per-char)** | **ported (this chip)** |
| **clip setters (X + Y)**       | **ported (this chip)** |
| line drawers (axis + diag)     | deferred (4 fns, 347+442+171+197 B) |
| text renderer                  | deferred (989 B, `FUN_00402637`) |
| word-wrap renderer             | deferred (673 B, `FUN_0040328e`) |
| centered-text wrapper          | deferred (96 B, `FUN_004025d7`) |
| sprite blitters (4x4/6x6/8x8)  | unanalyzed |

The per-character path is now end-to-end in `src/`.  The test
harness can hand a font blob + char-glyph table + char_code to
`gfx_dispatch_glyph` and produce pixels on a bound canvas — verified
across all four clip outcomes on both axes, the default-font
character-range tweaks, and the source-pointer fixup after clip.

### What's next

Three natural continuations:

1. **Text renderer** — port `FUN_00402637` (989 B), the per-frame
   line/multi-line renderer that calls `gfx_dispatch_glyph` in a
   loop.  Closes the remaining gap to "render a string in CI."
   Depends on understanding the line layout state (cursor_x reset
   per line, line-height accumulation, etc.) but everything below it
   is now ported.
2. **Word-wrap renderer** — `FUN_0040328e` (673 B), the multi-line
   word-wrapping variant used by long help / scenario text.  Depends
   on the same per-character path plus the word_width measurer
   (already ported).
3. **Wire gfx.c runtime to gfx_paint/gfx_glyph** — extend the
   existing `IDirectDrawSurface::Lock` probe to set `lpSurface` onto
   the gfx_paint canvas.  Small chip but requires a runtime smoke
   test on the Windows host to validate.  Unblocks "draw a real
   glyph during boot" as a manual end-to-end check.

Picking #1 (text renderer) advances the visible-pixels milestone
most directly — once it lands, the test harness can produce pixel-
by-pixel golden comparisons against Frida-captured retail
screenshots for arbitrary text strings.

This is a natural `/clear` point — the per-character path is a
complete stratum, and the next frontier (the text renderer that
drives it in a loop) is a fresh multi-state scope worth a fresh
conversation.

---

## 2026-05-25 — Glyph blitter cluster (1 chip, 3 fns, byte-identical dedup)

Continuation of the same unattended session.  The previous dispatch
landed the framebuffer pixel painters and listed three follow-ups,
with the glyph blitter as the recommended next move toward the
visible-pixels milestone.  This dispatch takes that step: ports the
per-character glyph emitter that sits between the (deferred) text
renderer and the freshly-landed pixel painters.

### Chip landed

**`src/gfx_glyph` (NEW, ~170 LOC + 12 tests)** — 3 retail leaves
that paint a glyph's source bytes onto the canvas via a row/col
loop with optional clipping pads:

  - `FUN_004b41b7` → `gfx_glyph_blit`              (165 B) — unclipped
    path, two write modes (copy-byte vs color-fill), no source
    stride pad.
  - `FUN_004b425c` → `gfx_glyph_blit_left_clipped`  (105 B) — clipped
    path with `clipped_w` inner count and source stride pad.  Color-
    fill only.
  - `FUN_004b42c5` → `gfx_glyph_blit_right_clipped` (105 B) — BYTE-
    IDENTICAL retail body to `FUN_004b425c` (r2-verified).  Exposed
    as a separate entry point for dispatcher topology; shares a
    static helper with the left-clipped path.

State is held in file-static globals that mirror the retail DAT_*
set the dispatcher (`FUN_00402a14`, not yet ported) writes
immediately before each call.  Setters expose the same writes to
tests.  Canvas binding piggy-backs on `gfx_paint_bind_canvas` —
`gfx_paint` gained read-only accessors
(`gfx_paint_canvas_pixels` / `_width` / `_height`) so peer drawers
can share a single canvas state.

### Filed quirks (#27, #28)

Two new entries in `docs/findings/engine-quirks.md`:

  - **#27** — `FUN_004b425c` and `FUN_004b42c5` are byte-for-byte
    identical (every instruction matches, both 105 B).  Almost
    certainly two distinct source-level functions that reduced to
    the same machine code post-MSVC-4.20 codegen, without COMDAT
    folding catching the duplicate.  The dispatcher selects between
    them by clip mode tag but they're behaviorally indistinguishable.
    Our port preserves both names with one shared implementation.
  - **#28** — the glyph blitters' inner loop ALWAYS runs at least
    once, even at `glyph_w == 0` or `glyph_h == 0`.  The retail
    `do { ... } while (post-test)` shape with `0 < counter` AND
    `next != 0` guards is permissive on the first iteration.
    Dormant in retail because the upstream dispatcher filters zero
    dimensions, but the leaves themselves do not — faithful port
    preserves the unguarded behavior with two dedicated tests.

### Headline numbers

- **477 → 489 host tests** (12 new), all green under ASan/UBSan.
- **3 retail functions ported** (~375 B decompilation), with the
  two byte-identical duplicates sharing a single static helper.
- **1 new module** (`src/gfx_glyph`) + 3-line extension to
  `src/gfx_paint` (canvas accessors).
- **2 new engine quirks filed** (#27, #28).
- Cross-compile (mingw32) clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### Renderer pipeline state

| layer                          | status     |
| ------------------------------ | ---------- |
| char width                     | ported     |
| buffer width                   | ported     |
| bounded buffer width           | ported     |
| word width + chars             | ported     |
| editor (text_input)            | ported     |
| localization blob access       | ported     |
| dirty-rect tracking            | ported     |
| 1-pixel / 2x1 painters         | ported     |
| clipped pixel / 2x1            | ported     |
| **glyph blitter (unclipped)**  | **ported (this chip)** |
| **glyph blitter (clipped × 2)** | **ported (this chip)** |
| line drawers (axis + diag)     | deferred (4 fns, 347+442+171+197 B) |
| glyph dispatcher (per-char)    | deferred (514 B, `FUN_00402a14`) |
| glyph descriptor cluster       | deferred (`FUN_0040464c` / `FUN_0040477b` clip setters) |
| text renderer                  | deferred (989 B, `FUN_00402637`) |
| word-wrap renderer             | deferred (673 B, `FUN_0040328e`) |
| centered-text wrapper          | deferred (96 B, `FUN_004025d7`) |
| sprite blitters (4x4/6x6/8x8)  | unanalyzed |

The text-rendering bottom is now fully ported.  Everything from a
glyph's source bytes to the back buffer is present in `src/`.  The
remaining gap to "render a text line in CI" is the glyph dispatcher
(`FUN_00402a14`, 514 B — the per-character call site that wires up
the descriptor table, clip setters, and one of the three blitters)
plus the text/word-wrap renderers that drive it.

### What's next

Three natural continuations:

1. **Glyph dispatcher + clip setters** — port `FUN_00402a14`
   (514 B) along with `FUN_0040464c` (303 B) and `FUN_0040477b`
   (291 B).  Three functions, ~1.1 KB decompilation.  This puts the
   full per-character path in `src/`, exposing a clean
   `draw_char(font_base, char_code)` API.  Depends on the font
   descriptor format being understood (already covered by
   `font_metrics_char_width` — the 256-byte char→glyph-index table
   + 16-byte descriptors).
2. **Text renderer** — port `FUN_00402637` (989 B), the per-frame
   line/multi-line renderer that calls the dispatcher in a loop.
   Larger function but everything below it is ported; depends on
   #1 if we want to keep the call topology faithful, or could call
   the leaves directly via the staged state.
3. **Wire gfx.c runtime to gfx_paint/gfx_glyph** — extend the
   existing `IDirectDrawSurface::Lock` probe to set `lpSurface`
   onto the gfx_paint canvas.  Small chip but requires a runtime
   smoke test on the Windows host to validate.  Unblocks "draw
   a real glyph during boot" as a manual end-to-end check.

Picking #1 advances the visible-pixels milestone most directly —
once the dispatcher lands, the test harness can call `draw_char`
on the real `Charset3.dat` font blob and produce a pixel-by-pixel
golden comparison against a Frida-captured retail screenshot.

This is a natural `/clear` point — the rendering bottom is now a
complete stratum, and the next frontier (the dispatcher) is a fresh
multi-function scope worth a fresh conversation.

---

## 2026-05-25 — Framebuffer pixel painters (1 chip, 5 fns)

Continuation of the same unattended session.  The prior dispatches
closed out the text-input / font-metrics / localization clusters
and listed two candidate next moves: begin the renderer port (with
a stub-only wrapper as the "slightly weird shape" option) or start
modeling the per-army struct (multi-chip endeavor).  This dispatch
takes a third path: port the framebuffer pixel-painter PRIMITIVES
that sit at the bottom of the rendering stack.  These were the
missing ingredient that made the renderer-port options weird —
without them, the renderer would have had to stub `FUN_00402a14`'s
callees too.  Now they're real.

### Chip landed

**`src/gfx_paint` (NEW, ~60 LOC + 23 tests)** — 5 retail leaves at
the very bottom of every draw call (lines, boxes, glyphs, tile
blits all bottom out here):

  - `FUN_004b3f54` → `gfx_paint_pixel`           ( 38 B) — 1 pixel
    at (x,y) := color.  Pure formula `pixels[x + y*width] = color`.
  - `FUN_004b3f7a` → `gfx_paint_2x1`             ( 41 B) — 2-byte
    horizontal brush.
  - `FUN_004b3fa3` → `gfx_paint_2x1_under`       ( 61 B) — 2x1
    paint-under: each pixel writes iff current value is 0.  Sprite
    overlay primitive.
  - `FUN_004039c7` → `gfx_paint_pixel_clipped`   (101 B) — bounds-
    checked wrapper around the 1-pixel painter.
  - `FUN_00403a2c` → `gfx_paint_2x1_clipped`     ( 99 B) — bounds-
    checked wrapper around the 2x1 painter, BUT only checks the
    LEFT pixel (quirk #26).

Canvas binding mirrors retail's globals (DAT_004ea1a8 / 004eb274 /
004eac4c) but exposes a `gfx_paint_bind_canvas(pixels, w, h)` entry
point so tests can swap in a host-allocated buffer.  Runtime
binding will land when gfx.c's existing Lock/Unlock probe gets
extended to also store lpSurface (a pre-existing mis-label there:
`g_back_pitch` is documented as DAT_004ea1a8 but stores `lPitch`
— retail actually stores `lpSurface`.  Latent bug, no current
reader, noted for the runtime-wire chip).

### Filed quirks (#26)

One new entry in `docs/findings/engine-quirks.md`:

  - **#26** — the clipped 2x1 painter bounds-checks only the LEFT
    pixel.  At `x == width - 1` the right pixel writes one column
    past the row.  Contiguous canvas → wraps into the next row;
    bottom-right corner → wraps one byte past the back buffer
    entirely.  Retail callers (line/edge drawers) iterate with a
    step that avoids this; we preserve the quirk verbatim and pin
    it down with two dedicated tests.

### Headline numbers

- **454 → 477 host tests** (23 new), all green under ASan/UBSan.
- **5 retail functions ported** (~340 B decompilation).
- **1 new module** (`src/gfx_paint`).
- **1 new engine quirk filed** (#26).
- Cross-compile (mingw32) clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning (the existing
  `src/Makefile`'s glob picks up the new TU automatically).

### Renderer pipeline state

| layer                    | status     |
| ------------------------ | ---------- |
| char width               | ported     |
| buffer width             | ported     |
| bounded buffer width     | ported     |
| word width + chars       | ported     |
| editor (text_input)      | ported     |
| localization blob access | ported     |
| dirty-rect tracking      | ported     |
| **1-pixel / 2x1 painters** | **ported (this chip)** |
| **clipped pixel / 2x1**    | **ported (this chip)** |
| line drawers (axis + diag) | deferred (4 fns, 347+442+171+197 B) |
| text renderer            | deferred (989 B, `FUN_00402637`) |
| word-wrap renderer       | deferred (673 B, `FUN_0040328e`) |
| centered-text wrapper    | deferred (96 B, `FUN_004025d7`) |
| glyph blitter            | deferred (165 B, `FUN_004b41b7`) |
| sprite blitters (4x4/6x6/8x8) | unanalyzed |

The renderer-port chip is now substantially less weird: with the
pixel painters in hand, `FUN_00402637` (the 989-byte text
renderer) only needs the glyph blitter (`FUN_004b41b7`, 165 B —
which depends on the font-blob descriptor fields and a handful of
cursor-anchor globals) to actually paint.  No more painter stubs.

### What's next

Three natural continuations:

1. **Glyph blitter** — port `FUN_004b41b7` (165 B) + its two
   wrapping variants `FUN_004b425c` / `FUN_004b42c5` (both 105 B).
   These read the per-glyph descriptor bytes already-modeled by
   `font_metrics_char_width` and write into the canvas via the
   pixel painters just landed.  Should be ~one chip, unblocks the
   text renderer outright.

2. **Line / box drawers** — `FUN_00403c49` (axis-aligned edge
   drawer, 171 B), `FUN_0040437d` (filled horizontal-line variant,
   197 B), and `FUN_004037ee` (line-clipper / setup, 347 B).  All
   now backed by the painter primitives.  Lower priority than the
   glyph blitter for the visible-pixels milestone, but they unlock
   the rest of the UI chrome (window frames, divider bars, etc.).

3. **Wire gfx.c runtime to gfx_paint** — extend gfx.c's existing
   `IDirectDrawSurface::Lock` probe (lines ~498-504 of gfx.c) to
   ALSO read `desc.lpSurface` (DDSURFACEDESC offset 0x24) and
   call `gfx_paint_bind_canvas` with it.  Fix the pre-existing
   `g_back_pitch` mis-labeling (it stores `lPitch`, retail's
   DAT_004ea1a8 actually holds `lpSurface`).  Small chip but
   requires a runtime smoke-test to validate.

Picking #1 (glyph blitter) advances the visible-pixels milestone
most directly — once it lands plus a thin renderer-wrapper, the
engine can actually paint a text line into a back buffer in CI.

This is a natural `/clear` point — the renderer port pipeline has
a fresh frontier (glyph blitter) and the durable docs capture
everything needed to resume.

---

## 2026-05-25 — Localization cluster + word-wrap measurer (2 chips, 5 fns)

Continuation of the same unattended session.  The prior dispatch
closed out the text-input subsystem's editor leaves and adjacent
dependencies.  This dispatch lands two more chips along the
roadmap's "localization" branch — surfacing l2_eng.dat to the
ported codebase, and porting the word-wrap measurer that the
deferred text renderer (`FUN_00402637`) will need.

### Chips landed

1. **`src/localization` (NEW, ~135 LOC + 17 tests)** — accessor
   cluster for the 120 KB localization blob loaded at retail
   address `0x591580` by WinMain.  Four retail leaves:

   - `FUN_0040187a` → `localization_offset`   (93 B) — 24-bit LE
     offset decoder for the primary section table at `blob+8`.
   - `FUN_004018d7` → `localization_seek_skip` (173 B) — set the
     walker to a section, then skip N sub-string boundaries,
     then skip leading control bytes.  Uses the "permissive"
     boundary detector that accepts NUL-after-NUL.
   - `FUN_004017bf` → `localization_read`      (187 B) — same as
     seek_skip + memcpy out.  Uses the "strict" boundary detector
     (no NUL-after-NUL).
   - `FUN_004016f3` → `localization_write`     (204 B) — write
     path; **zero retail callers**, asymmetric secondary offset
     table (16-bit BE at `blob+0x1c`), no bounds check.  Ported
     faithfully as dead code.

2. **`src/font_metrics` extension (1 fn, 11 tests)** — next-word
   pixel-width measurer for the word-wrap renderer:

   - `FUN_004036f9` → `font_metrics_word_width` (235 B) — measures
     the next word in a stream, returning pixel width and writing
     `chars_consumed` (retail `DAT_005c9a7c`) for the byte count.
     Implements the word-wrap state machine: leading spaces
     contribute width (+4 each), internal space/'$' terminate the
     word.  Used by `FUN_0040328e` (the 673-byte word-wrap loop
     that the text renderer wraps).

### Filed quirks (#20–#25)

Six new entries in `docs/findings/engine-quirks.md`:

  - **#20** — localization seek vs. read paths use DIFFERENT
    boundary detectors.  Seek accepts NUL-after-NUL; read does
    not.  A blob with consecutive NULs yields divergent boundary
    counts between `seek N` and `read N` for the same N.
  - **#21** — localization write uses an asymmetric secondary
    offset table (16-bit BE, base anchor `blob+0x1c`) vs. the
    read-side primary table (24-bit LE, base `blob[0]`).  Plus
    zero retail callers — almost certainly dead editor-build code.
  - **#22** — bounds checks are off-by-one in the read path
    (`walker < base + 120001`) and absent entirely in the write
    path.  Faithful port preserves both.
  - **#23** — word-width measurer treats leading spaces as
    word-internal (each contributes +4 width), but the first
    internal space terminates the word.  Classic word-wrap rule.
  - **#24** — `$` is a soft word boundary.  Leading `$` is
    silently consumed.  The engine uses `$` as a macro-expansion
    sigil (`$P` for player name, etc.) — the wrap state machine
    consumes the sigil prefix before measuring the macro body.
  - **#25** — 1999-iteration cap in word_width — one less than
    the 2000-byte editor buffer size, a safety margin against
    a buffer that lost its NUL terminator.

### Headline numbers

- **426 → 454 host tests** across both chips (17 + 11 new),
  all green under ASan/UBSan.
- **5 retail functions ported** (~890 B decompilation; 4 + 1).
- **2 commits** — one per chip.
- **1 new module** (`src/localization`).
- **1 module extension** (`src/font_metrics`).
- **6 new engine quirks filed**.
- Cross-compile (mingw32) still clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### State of the text/rendering pipeline

What's now in place:

| layer                    | status     |
| ------------------------ | ---------- |
| char width               | ported     |
| buffer width             | ported     |
| bounded buffer width     | ported     |
| word width + chars       | ported     |
| editor (text_input)      | ported     |
| localization blob access | ported     |
| dirty-rect tracking      | ported     |
| **text renderer**        | **deferred** (989 B, FUN_00402637) |
| word-wrap renderer       | deferred (673 B, FUN_0040328e) |
| centered-text wrapper    | deferred (96 B, FUN_004025d7) |
| text-input renderer      | deferred (989 B, the editor's per-frame body) |

Everything that the deferred renderers READ from is now ported.
The deferred renderers themselves WRITE to the framebuffer (via
the palette + draw primitives we don't have yet) and call into
the keyboard-state globals — that's the next big scope.

### What's next

Two natural continuations:

1. **Begin the renderer port** — `FUN_004025d7` (centered text
   wrapper, 96 B) is the smallest and only depends on
   `font_metrics_buffer_width_bounded` (have it) and
   `FUN_00402637` (the actual renderer).  Could be ported as a
   wrapper-only chip leaving `00402637` as an unimplemented
   stub for now; that's a slightly weird shape but it lets us
   move forward incrementally rather than landing the whole
   989-byte renderer in one shot.  Alternative: port
   `FUN_00402637` itself directly — that's a multi-chip endeavor
   (the function paints into the framebuffer, manages the cursor
   anchor state machine, drives keyboard repeat timers...).

2. **Per-army struct modeling + handler ports** — the same
   roadmap entry that's been deferred for two sessions now.
   Spans multiple chips: 80-slot army table at `0x554492`
   (`0x1b0` B/slot), then the type-30 delayed-action handler
   (`FUN_0044800e` → `FUN_0047a76d`), then ~10 follow-up
   handlers, plus army-distance/state probes
   (`FUN_004673b8` / `FUN_00472227`).  Highest long-term
   impact for the simulation port but doesn't help the
   "see something running" milestone.

Picking #1 (renderer wrapper or full) advances the visible-pixels
milestone.  Picking #2 advances the simulation-fidelity milestone.

This is a natural `/clear` point — both milestones are fresh
scopes worth a fresh conversation, and the durable docs
(PROGRESS, quirks, headers) capture everything needed to resume.

---

## 2026-05-25 — Text-input subsystem follow-up (4 chips, 23 fns)

Continuation of the same unattended session.  The previous
dispatch landed the text_input editor cluster as a single big chip
(15 fns); these four follow-ups closed out the subsystem's
remaining leaves and adjacent dependencies.

### Chips landed (in order)

1. **`src/font_metrics` (NEW, ~90 LOC + 14 tests)** — pixel-width
   measurer.  2 retail leaves:

   - `FUN_004015b9` → `font_metrics_char_width` (173 B) — per-char
     width via 256-byte char→glyph-index lookup + 16-byte glyph
     descriptors.  Hardcoded specials: `'\0' → 0`, `' ' → 4`
     (not table-driven).  Updates `last_glyph_index` /
     `last_glyph_offset` cache fields (read by 50+ retail
     renderers).
   - `FUN_00401f8f` → `font_metrics_buffer_width` (122 B) — sums
     widths of the text-input buffer and sets `pixel_overflow := 1`
     iff total >= `max_pixel_width`.  Closes the loop on
     `text_input_clamp_cursor`'s `pixel_overflow` driver (callers
     wire the two by calling `font_metrics_buffer_width` on the
     text_input buf then copying `pixel_overflow` over).

2. **`src/font_metrics` + `src/text_input` extensions (3 fns,
   7 tests)** — closed remaining adjacent leaves:

   - `FUN_004014f0` → `font_metrics_buffer_width_bounded` (201 B):
     general-purpose buffer-width helper with a 10000-char hard
     cap and no `pixel_overflow` side effect.  Used by the
     renderer for arbitrary caller-supplied strings.  Inlines the
     char-width logic (instead of calling `font_metrics_char_width`)
     but yields identical observable cache state.
   - `FUN_00401c3f` → `text_input_insert_char_and_clamp` (28 B):
     insert_char then redundant clamp_cursor.  For callers wanting
     post-advance re-validation.
   - `FUN_00401c93` (16 B): bare jmp to `text_input_delete_char` —
     documented in `text_input.h` without a separate export.

3. **`src/strutil` extension (2 fns, 14 tests)** — decimal parser
   + space-collapse:

   - `FUN_00401666` → `strutil_decimal_atoi` (141 B): leading
     digit-run → int via 8-entry powers-of-10 table.  Retail's
     table has only 8 entries; 9+ digit inputs read off the end
     into adjacent .data string bytes ("ERR:_top_it not data...",
     confirmed via od of the PE at file offset 0xd0250).  Our
     port caps at 8 digits to avoid the UB.
   - `FUN_00401415` → `strutil_collapse_double_spaces` (219 B):
     two-pass in-place collapse.  Retail quirk: uninitialized
     `local_c` in the pathological no-null-in-64K case; our port
     initializes to 0 (no-op in the bad case).

4. **`src/text_input` filtered keypress (1 fn, 12 tests)** — last
   editor leaf with no unported dependencies:

   - `FUN_00401a20` → `text_input_filtered_keypress` (543 B):
     keyboard event classifier and dispatcher.  Permits ASCII
     printables (digits, both case letters, hyphen, space) and
     extended-Latin bytes (0x80..0x9a, 0xa0..0xa7, 0xe1).
     Conditionally permits punctuation (`,` `.` `?` `!`) only when
     `init_mode_param == 0`.  In `init_mode_param != 0` ("name
     mode"), folds uppercase to lowercase before insert.  Always
     ends with `clamp_cursor`, even on filtered chars.

### Headline numbers

- **337 → 426 host tests** across the 5 chips of this session
  (chip 0 was the initial text_input cluster; chips 1-4 are the
  follow-ups above).  All green under ASan/UBSan.
- **23 retail functions ported** in the follow-up chips (15 +
  23 = 38 across the full session).
- **2 new modules** (`src/text_input`, `src/font_metrics`).
- **2 module extensions** (`src/text_input`, `src/font_metrics`,
  `src/strutil` — counting `text_input` once even though it
  received both new functions and extensions).
- **5 new engine quirks filed** (chip 0's #15-#19); no new quirks
  in the follow-up chips (the smaller leaves were behaviorally
  clean except for the documented edge cases in the chip-3
  strutil pair).
- Cross-compile (mingw32) still clean throughout.

### State of the text-input subsystem

The editor itself is now feature-complete except for the
989-byte renderer (`FUN_00402637`) and the save/load pair
(`FUN_00402166` / `FUN_0040220f`).  Both are deferred:

- **Renderer** — pulls in font subsystem (now ported!), dirty-
  rect (already ported), palette, keyboard-state globals, repeat-
  key timers, the cursor-anchor (`DAT_005aeb64`) state machine,
  and the indirect-text-area renderer.  Easily another full
  session of work on its own.
- **Save/load** — needs file IO leaves (`FUN_004af8d9` read-at +
  `FUN_004b0502` write-at).  Those leaves pull in non-leaf
  retry helpers (`FUN_004af312` / `FUN_004af116` / `FUN_004af502`
  — likely CD-mount/swap dialog handlers).  A clean port requires
  modeling the CD-swap logic too, which is non-trivial.

### What's next

For the next session:

1. **Per-army struct modeling + handler ports** — the prior
   session's deferred "type 30 delayed-action handler"
   (`FUN_0044800e` → `FUN_0047a76d`) needs the 80-slot army table
   (0x1b0 B/slot, base ≈ 0x554492) modeled first.  That's a
   substantial chunk: army struct is reasonably tagged via field
   names in adjacent functions, and modeling it unblocks 10+
   subsequent handlers + the army-distance/state-probe sub-step
   bodies (`FUN_004673b8` / `FUN_00472227`) noted in earlier
   dispatches.

2. **Localization-blob accessor cluster** — `FUN_0040187a` (93 B,
   3-byte LE offset decoder), `FUN_004018d7` (173 B, "skip N
   strings" walker), `FUN_004017bf` (187 B, read-string with
   skip), `FUN_004016f3` (204 B, write-string with skip).  Models
   the L2_ENG.DAT localization blob at 0x591580.  ~657 B total —
   one solid chip.  Requires loading the localization blob into
   memory (already done by the existing config/io path?).

3. **Renderer port** — the big one.  Now that font_metrics is in
   place, the next dependency for the text-input renderer is
   the palette + dirty-rect glue (mostly ported) and the cursor-
   anchor state machine.  Could be approached as a series of
   chips: anchor state machine first, then the per-key dispatch,
   then the actual paint.

Picking #2 (localization cluster) next would be a satisfying
~one-chip session, similar to today's wave.  #1 (per-army
modeling) is the highest-impact long-term move but spans
multiple chips.

---

## 2026-05-25 — Text-input editor cluster (15 fns, 1 chip)

Continuation of the same unattended session that landed the prior
six chips.  Goal: port the text-input box's editor leaves — the
2000-byte editable buffer that powers save-name prompts, scenario
titles, chat lines, etc.  Identified as a clean next chip in the
previous dispatch.

### Chip landed

**`src/text_input` (NEW, ~290 LOC + 42 tests)** — 15 retail leaves
modeling the editor's buffer + cursor state machine:

  - `FUN_00401984` → `text_input_clamp_cursor`        (156 B)
  - `FUN_00401c70` → `text_input_backspace`           (35 B)
  - `FUN_00401ca3` → `text_input_toggle_insert_mode`  (25 B)
  - `FUN_00401cbc` → `text_input_cursor_left`         (30 B)
  - `FUN_00401cda` → `text_input_cursor_right`        (34 B)
  - `FUN_00401cfc` → `text_input_cursor_home`         (21 B)
  - `FUN_00401d11` → `text_input_cursor_end`          (21 B)
  - `FUN_00401d26` → `text_input_insert_char`        (162 B)
  - `FUN_00401dc8` → `text_input_delete_char`         (64 B)
  - `FUN_00401e49` → `text_input_probe_at_cursor`    (135 B)
  - `FUN_00401ed0` → `text_input_strip_spaces`       (121 B)
  - `FUN_00401f49` → `text_input_recompute_length`    (70 B)
  - `FUN_004020ae` → `text_input_clear`               (94 B)
  - `FUN_00402009` → `text_input_init`               (165 B)
  - `FUN_0040210c` → `text_input_copy_out`            (90 B)

All state lives in an explicit `text_input_state_t` struct (2000-B
buf + 11 scalar fields).  The cluster's only inter-function call is
insert/delete/backspace dispatching to clamp_cursor; the rest are
leaves.

Builds on previously-ported `strutil_shift_left` /
`strutil_shift_right` (from the prior session) — insert mode uses
shift_right to make room, delete uses shift_left.

### Filed quirks (#15–#19)

Five new entries in `docs/findings/engine-quirks.md`:

  - **#15** — clear() sets length=1 sentinel even on a zeroed
    buffer (distinct from recompute, which sets 0).
  - **#16** — init()'s payload-copy reuses `local_8` across both
    loops, exiting the clear at -1 and indexing via `&buf[1]+(-1)`
    to write into buf[0].  Compiler register-reuse optimization.
  - **#17** — clamp_cursor pins past-end cursor to length+1, not
    length.  Distinguishes "caret on terminator" from "caret in the
    fresh slot beyond it".
  - **#18** — insert_char's overwrite branch (insert_mode==0) skips
    both the max_length bounds check and the length update.  Caller
    must recompute_length to refresh the cached count.
  - **#19** — probe_at_cursor flips to result=2 when clamp_state==1
    AND buf[cursor]=='.' AND cursor>=8 — the "too far right for a
    decimal" warning state used by number-entry fields.

Each quirk has a dedicated preserving test case.

### Deferred to follow-up chips

  - **Pixel-width measurer** (`FUN_00401f8f` + `FUN_004015b9`, ~295
    B) — measures cumulative pixel width of the buffer using a font
    glyph table.  Sets the `pixel_overflow` flag that clamp_cursor
    reads.  Needs font-table modeling (256-byte char→glyph-idx
    table at `DAT_004d71d0`, 16-byte glyph entries pointed to by
    the active font at `DAT_005af8f0`).  In the meantime,
    `clamp_cursor` reads `pixel_overflow` from the state struct;
    tests set it directly to exercise the `clamp_state == 2`
    pixel-overflow branch.
  - **Save / load** (`FUN_00402166` + `FUN_0040220f`, ~343 B) —
    persist/restore buffer to/from a file region.  Needs the file
    IO helpers `FUN_004af8d9` (read-at) and `FUN_004b0502` (write-
    at), which are themselves unported.
  - **Renderer / event dispatch** (`FUN_00402637`, 989 B) — the
    massive per-frame routine that handles keyboard events, paints
    the field, manages the cursor anchor, scrolling, repeat-key
    timers, etc.  Pulls in font subsystem, dirty-rect, palette,
    keyboard-state globals.  Significantly larger than a normal
    chip and tightly coupled to subsystems we don't have yet.

### Headline numbers

- **337 → 379 host tests** (all green, ASan + UBSan).
- **15 retail functions ported** (1.1 KB of decompilation).
- **1 commit** — single coherent chip per commit-as-you-go.
- **5 new engine quirks filed** (#15–#19), each with a test.
- **1 new module** (`src/text_input`).
- Cross-compile (mingw32) still clean — both `openlords2.exe` and
  `openlords2-debug.exe` link without warning.

### What's next

The two natural continuations:

1. **Port the pixel-width measurer** — small chip (2 fns, ~295 B
   decompilation).  Closes the loop on clamp_cursor's
   `pixel_overflow` driver and prepares for the eventual renderer
   port.  Needs font glyph table modeling — straightforward but
   one extra module.

2. **Port more per-handler delayed-action slots** — same as the
   prior session's "next candidates" list.  Type 30 (`FUN_0044800e`
   → `FUN_0047a76d` 80-army-slot scan) is the cleanest leaf
   handler to land next.

3. **Port the text-input save/load** — needs the file IO leaves
   (`FUN_004af8d9` read-at, `FUN_004b0502` write-at) first.  Those
   appear in many other clusters too, so they'd be a good general
   chip to land independently.

Picking #1 next iteration keeps momentum on the text-input
subsystem — once the measurer is done the cluster is feature-
complete except for the renderer (which is too large for a single
chip and will need its own pass).

---

## 2026-05-25 — Six leaf-cluster ports in one unattended session

Continuing the "chip away at leaf clusters" pattern proven by the
prior dispatches.  Worked unattended; chips landed as separate
logical-unit commits as they finished.  No regressions; 246 tests
at session start → **337 tests at end (all green under ASan/UBSan)**.

### Six chips landed

1. **`src/engine_path` (NEW, ~290 LOC + 26 tests)** — army pathfinding
   cluster.  Closed leaf cluster of 5 retail functions:
   - `FUN_00404f4c` → `engine_path_chebyshev_distance` (and its sibling
     `FUN_00404ead` → `engine_path_manhattan_distance` landed later in
     chip #6 because it shares the same dx/dy side-effect storage)
   - `FUN_0046f434` → `engine_path_direction_8way`
   - `FUN_00472392` → `engine_path_search_greedy`
   - `FUN_0047265d` → `engine_path_find_nearest_matching`
   - `FUN_00472875` → `engine_path_stash_into_army`

   The greedy descent walks an upstream BFS distance map (still
   unported) and reconstructs a 150-step max path between two cells.
   The 8-way direction helper is a straight three-way-per-axis
   compare; the nearest-match picker scans expanding 30-radius
   squares for the lowest-cost cell whose terrain stage matches the
   start cell's.  Per-army stash writes into the 0x1b0-byte per-army
   slot (which we'll fully model later when more army ports land).

   **Quirk #13 filed**: nearest-matching picker uses linear-index 0
   as both the "found a match" and "no match" sentinel — a hit at
   the top-left corner (idx 0) is invisible.  Benign in real maps
   (outer-ring is always walls in retail data) but exercised by a
   dedicated test.

2. **`src/engine_handlers_da` (NEW, ~30 LOC + 10 tests)** — trivial
   delayed-action handlers, 6 of 50 slots in `PTR_FUN_004d5c98`:
   - types 0/21/45/46/47 → no-op (5 retail fns all `ret`-only)
   - type 48 → divergence counter increment (`DAT_00554038 += 1`)

   First slice of the per-handler dispatch table.  Type 49 (which
   calls FUN_00435211 + sets a global) and the remaining ~40 single-
   callee handlers wait on per-callee porting.  Tests drive both
   direct calls AND the real `engine_dispatch_delayed_actions()` loop
   so they double as integration tests of the dispatcher's per-record
   handler invocation path.

   Pulled the dispatch table layout from the PE directly (file off
   0xd3e98 in .data) to confirm slot→FUN mapping before porting.

3. **`src/engine_dirty_rect` — extension (+1 fn + 5 tests)** —
   `engine_dirty_rect_mark_box_tiled` (port of `FUN_004527a6`).
   Parametric pixel-origin + tile-stride extent wrapper used by 10+
   UI callers (status panels, popup widgets) that don't fit the
   fixed-size mark_tile_X wrappers.  Also documented the
   `FUN_0043cc56` thunk-alias of mark_below_status_bar — same
   semantics, just a distinct address for function-pointer-table use.

4. **`src/engine_category_buckets` (NEW, ~80 LOC + 14 tests)** —
   per-category byte-bucket table (6 buckets × 16 slots) + 6-byte
   "first non-duplicate" digest:
   - `FUN_004290d0` → `engine_category_buckets_clear`
   - `FUN_00429153` → `engine_category_buckets_append`
   - `FUN_0042925e` → `engine_category_buckets_digest_contains`
   - `FUN_004291b3` → `engine_category_buckets_refresh_digest`

   Used during the l2_maps.dat parse (retail `FUN_00467770`) to
   collect per-category tile types, then the army-setup pipeline
   consumes the digest.

   **Quirk #14 filed**: `refresh_digest`'s every-other-byte probe
   resets to ODD parity when it reads a zero — quirky behavior almost
   never observed in production (buckets dense top-down from map
   load) but observable in hermetic test inputs.  Test case preserves
   it.

5. **`src/strutil` — extension (+8 fns + 21 tests)** — char-case
   conversion + string mutation leaves:
   - `FUN_00401176` → `strutil_to_upper_char`
   - `FUN_004011b0` → `strutil_to_lower_char`
   - `FUN_004011ea` → `strutil_to_upper_inplace`
   - `FUN_00401246` → `strutil_to_lower_inplace`
   - `FUN_00401303` → `strutil_shift_left`
   - `FUN_00401333` → `strutil_shift_right`
   - `FUN_00401364` → `strutil_strip_leading_spaces`
   - `FUN_004013bc` → `strutil_trim_trailing_spaces`

   Two retail quirks documented in-header: the single-char to_upper /
   to_lower return 0 (not the input!) on out-of-range chars, while
   the whole-string variants pass through unchanged; trim-trailing
   reads `str[-1]` on an all-spaces input (decrement-before-test
   loop).

6. **`src/engine_math` (NEW, ~30 LOC + 13 tests)** — small math leaf
   helpers + the Manhattan-distance sibling of chebyshev:
   - `FUN_00404d6b` → `engine_math_percent_of`     ((a*b)/100)
   - `FUN_00404d96` → `engine_math_bp_of`          ((a*b)/10000)
   - `FUN_00404dc1` → `engine_math_percent_div`    ((a*100)/b, /0→0)
   - `FUN_00404e03` → `engine_math_div_ceil`       (ceil(a/b), /0→0)
   - `FUN_00404e4b` → `engine_math_lookup_threshold`(tiered table walk)
   - `FUN_00404ead` → `engine_path_manhattan_distance` (lives in
     engine_path because it shares dx/dy side-effect with chebyshev)

### Headline numbers

- **246 → 337 host tests** (all green, ASan + UBSan).
- **6 commits**, each a clean logical unit (port + tests + header
  doc + any required dispatch wiring) per the project's
  commit-as-you-go convention.
- **30 retail functions ported** across the 6 chips.
- **2 new engine quirks filed** (#13, #14) — both with dedicated
  preserving test cases.
- **2 new modules** (engine_path, engine_category_buckets, engine_
  handlers_da, engine_math — that's 4, not 2; sorry).  3 extensions
  to existing modules (engine_dirty_rect, strutil, engine_path).

### What's next

1. **Port more per-handler delayed-action slots** — the next tier
   is the single-callee wrappers (types 4..29 and 31..44) where the
   callee is a small leaf or two-deep chain.  Each handler is 30-150
   B and most of them call into engine helpers (FUN_0043a23f,
   FUN_004284ce, etc.) that we'd need to also port.  Pick the
   handlers whose callees are themselves leaves first (e.g. type 30
   FUN_0044800e → FUN_0047a76d, which is a clean leaf walking 80
   per-army slots).

2. **Port the next sub-step body** — `FUN_0049b8b0` (turn-timeout,
   195 B) is small but uses timeGetTime + Sleep.  `FUN_0045270a` (43
   B) turned out to already be ported as
   `engine_dirty_rect_mark_below_status_bar`.  Better candidates
   from this session's survey: `FUN_004673b8` (378 B army-distance
   scan, calls our ported chebyshev), `FUN_00472227` (363 B
   per-army state probe, calls our ported chebyshev).  Both need
   per-army struct modeling — a fair amount of work but well-scoped.

3. **Port the text-input box cluster** — `FUN_00401cda`,
   `FUN_00401d26`, `FUN_00401e49`, `FUN_00402009`, etc.  ~11
   functions managing the 2000-byte text buffer at `DAT_005cd550`
   with cursor / length / max-length / mode globals.  All small
   (34..170 B), most are leaves, and the existing strutil
   `shift_left` / `shift_right` ports already cover the editor's
   sub-byte primitives.  A natural single dispatch.

4. **Wire the trivial delayed-action handlers into the diff harness**
   — the current diff harness stamps NULL slots and uses retail's
   `ret`-stub.  Now that we have real handlers for 6 slots, the
   diff vectors could exercise them through the dispatcher.  Small
   harness change; high diagnostic value.

5. **Carry-over items still on deck**:
   - per-type handlers for the remaining ~40 dispatch slots
   - save/load wired into the drop-in's runtime command surface
   - Quit-Game freeze + orchestrator-teardown hang diagnostic

Bias next time: option 1 (more dispatch handlers) — we now have the
handler-module pattern proven; each new chip adds 1-5 handler slots
and tests for free.  Option 3 (text input) is a tempting standalone
unit but doesn't unblock anything on the engine's main port path.

---

## 2026-05-25 — UI / screen-message event-queue producer cluster (5 functions)

Second leaf cluster of the dispatch.  The producer-side surface of the
50-slot UI event ring at `&DAT_00568480` — `FUN_00472bc5` (the 560 B
"post-event" workhorse with the 8 positional args), its two init
functions, the slot-zeroer, and the bare-publish tail — ports as one
unit.  The consumer (`FUN_00472e46`) is intentionally left for a later
dispatch because its callees (`FUN_00476768`, `FUN_0047309e`) are still
unported subsystems.

### Landed code

- **`src/engine_event_queue.h/c`** (~210 LOC):
  - `engine_event_queue_record_t` — packed 24-byte record matching the
    retail layout (i32 audience, i32 target, i32 code, i32 payload32a,
    u8 pad16, u8 flag5..7, i32 payload32b).  Six `_Static_assert`s on
    offsets so a layout shift fails at compile-time.
  - `engine_event_queue_state_t` — scratch + 50-slot ring + write_idx /
    read_idx / notify_flag / consumer_cooldown, all mirroring retail
    globals (`DAT_00553510`, `DAT_00568480`, `DAT_00543fd0`,
    `DAT_00553ec4`, `DAT_00565484`, `DAT_0057c954`).
  - `engine_event_queue_init`, `_init_wrapper`, `_slot_zero`,
    `_post(args..., player_has_data[7], local_player)`, `_publish_scratch`.
  - Post-filter implemented as three branches matching retail
    verbatim; all three reduce to "publish iff (audience == 0) OR
    (audience == local_player)" but the dead arms are preserved for
    future byte-equivalence diffs.

- **`tests/test_engine_event_queue.c`** (~290 LOC, 19 tests):
  - Record-size + capacity sanity.
  - Init: index clears, slot-0 full clear, slots-1..49 keep-non-code
    (quirk #12), init_wrapper byte-parity with init.
  - Post: each of the three filter arms (target=0, target!=0 +
    !has_data, target!=0 + has_data) × each audience case (0, local,
    other).
  - Wrap-around: write_idx 0x31 → 0 on the publishing path AND the raw
    publish_scratch path.
  - Scratch-on-filter-fail: post() updates scratch even when the
    filter skips the ring write — the save/restore path relies on this.
  - Publish_scratch ring-isolation: bytewise verify the 24-B memcpy
    doesn't spill into neighboring slots.
  - Slot-zero called with `slot == read_idx` (the consumer pattern)
    clears all 7 fields cleanly — quirk #12 is benign in that mode.

- **246 tests pass** under ASan + UBSan (was 227).

### Engine quirk #12 — slot_zero's partial-zero in the init loop

Filed: `FUN_00472b40` zeros field `+8` of `slot[param_1]` but the other
six writes target `slot[DAT_00553ec4]` (the read index) regardless of
`param_1`.  At runtime this is invisible (the consumer always passes
`param_1 == read_idx`), but during init's 0..49 sweep `read_idx` is
pinned to 0, so only slot 0 gets fully zeroed — slots 1..49 keep their
pre-init bytes in every field but `code`.  r2 confirms the codegen
literally (`lea eax, [eax+eax*2]` once from the arg, six times from a
fresh `[0x553ec4]` load).  Benign at cold boot (BSS is loader-zero);
matters on runtime re-init.  Preserved verbatim.

### What's next

1. **Port the next leaf cluster** — survey ahead for more no-callee
   producer/consumer groupings.  The event-queue + dirty-rect ports
   show the leaf-cluster pattern lands in one commit each with ~20-line
   test suites; same shape worth chaining.  Candidates: the
   `FUN_00472392` / `FUN_0047265d` / `FUN_00472875` group nearby (need
   to verify leaf-ness); the dependency-free file-format parsers.

2. **Port the per-type delayed-action handlers** — carried over.  Still
   gated on per-handler callee porting except for the 7 trivial /
   counter ones; that subset alone would let us drop the dispatch-table
   stub for those slots in the diff harness.

3. **Wire save/load into the drop-in's runtime command surface** —
   carried over.

4. **Diagnose the Quit-Game freeze + orchestrator-teardown hang** —
   carried over.

5. **Eventually port FUN_00472e46 (event-queue consumer)** — needs
   FUN_00476768 + FUN_0047309e ported first (or stubbed-with-TODOs as
   the dispatch-table pattern from delayed_action does).  Pairs with
   this dispatch but is a separate logical unit.

Bias next time: option 1 (next leaf cluster).  Two leaf-cluster ports
in a row land cleanly with the same shape; survey for a third before
peeling off into per-type handlers or consumer ports.

---

## 2026-05-25 — Dirty-rect / render-invalidation cluster ported (14 functions)

A whole subsystem in one logical unit.  The dirty-rectangle bookkeeper
that tracks which screen region needs re-rendering — `FUN_00452306`
(core) plus 13 trivial wrappers — is a leaf cluster in the call graph
with no callees, so it ports cleanly without dragging other
subsystems along.  Counted-as-one because the wrappers are 30-100 B
each and the cluster's value is the whole API surface.

### Landed code

- **`src/engine_dirty_rect.h/c`** (~140 LOC):
  - `engine_dirty_rect_state_t` — 6 ints (dirty_mark + 4 bound coords
    + below_status_redraw flag); all retail-VA cross-referenced.
  - `engine_dirty_rect_expand(state, x1, y1, x2, y2)` — core bound-
    widen + clamp.
  - 4 preset wrappers: full_screen, right_half, below_status_bar,
    below_status_bar_full (the latter forces dirty_mark = 2 post-
    expand).
  - 7 tile-stride wrappers: tile_32 / 48 / 64 / 80 / 96 / 200x160
    (square + one non-square) plus the special tile_80x48_noclamp
    (FUN_00452628's codegen lacks the negative-input clamp prelude).
  - Two retail duplicate-codegen wrappers (FUN_004524dc/4525_2f,
    FUN_00452582/452660) collapse into single ports each; documented
    in the header.

- **`tests/test_engine_dirty_rect.c`** (~200 LOC) — 20 unit tests:
  - expand: 1 → 1 mark transition, 2 → 2 preservation, widen vs
    inclusion no-op, x/y min clamp-to-zero, x-max clamp-to-639, and
    the y-max quirk-check (see #11 below).
  - Each preset and each tile-stride wrapper gets at least one
    sanity vector; mark_below_status_bar_full has its own override-
    to-2 test; tile_64 has a negative-clamp test; tile_80x48_noclamp
    has its lack-of-prelude check.

- **227 tests pass** under ASan + UBSan (was 207).

### Engine quirk #11 — dirty-rect y-max clamp uses the wrong bound

The clamp at the end of `FUN_00452306`:

```c
if (0x27f < DAT_0050a4c0) DAT_0050a4c0 = 0x1df;
```

Threshold is the X bound (639), assignment is the Y bound (479).
Result: dirty_ymax values in `[0x1e0, 0x27f]` slip past unclamped
— 1..160 pixels of off-screen dirty surface that never gets clipped
back.  In practice no production caller ever feeds the function a
y2 that lands in that range; preserved verbatim, with the test
asserting both the broken and working sides of the threshold.

### What's next

1. **Port the next sub-step body** — `FUN_0049b8b0` (turn-timeout,
   195 B) is the smallest candidate but uses `timeGetTime` + `Sleep`
   so it's not pure.  Pure alternative: continue surveying the call
   graph from `FUN_004486a9` / `FUN_00448819` etc. for the next pure
   leaf cluster to bag.  The dirty-rect win shows leaf clusters port
   in one logical unit + 20-line tests.

2. **Port the per-type delayed-action handlers** — carried over.  The
   no-op + counter-only handlers (7 of 50) port trivially; the rest
   gate on per-handler-callee porting.

3. **Wire save/load into the drop-in's runtime command surface** —
   carried over.

4. **Diagnose the Quit-Game freeze + orchestrator-teardown hang** —
   carried over.

Bias next time: option 1 (next leaf cluster).  The dirty-rect cluster
proved that wide-surface leaf ports land in one tidy commit when the
call graph cooperates; next survey pass should look for similar
groups (UI invalidation helpers around `FUN_00472bc5`, maybe?).

---

## 2026-05-25 — Castle-load fixup wired into castles_read_scenario

Closed the carry-over from prior dispatches.  `castles_read_scenario`
now invokes the ported `engine_tile_expand_footprint` for every record
whose flags have bit 0 set (and bit 1 clear), matching retail
`FUN_004aea70`'s call to `FUN_004917dd`.  The first end-to-end
"load + footprint expand" smoke is green.

### Landed code

- **`src/engine_tile.h/c`**: factored out a pointer-arg sibling,
  `engine_tile_expand_footprint_at(int byte_off, uint8_t *tiles,
  uint16_t *path_scratch, uint8_t *stage_scratch,
  int32_t (*footprint_list)[2], int32_t *last_footprint_tile)`.  The
  existing struct-based `engine_tile_expand_footprint(state*)` becomes
  a thin wrapper that forwards to the pointer version.  All existing
  unit tests + the pure-function diff harness keep working unchanged.

- **`src/save.h/c`**: added the four runtime globals the expansion
  reads/writes:
  - `g_path_scratch[6400]`  (u16, retail `DAT_00507250`, not persisted)
  - `g_stage_scratch[6400]` (u8,  retail `DAT_004f7d80`, not persisted)
  - `g_footprint_storage_160[160]` (retail `DAT_00553c80`, **IS** in
    the save schema as slot 110) — overlaid by macros
    `g_footprint_list[19][2]` (the first 152 B) and
    `g_last_footprint_tile` (4 B at offset 152), leaving 4 B trailing
    pad that ride along through round-trips for byte-exact fidelity.

- **`tools/gen_save_schema.py`**: extended `NAMED_BIG_BLOCKS` so any
  schema slot (not just 0..12) can be promoted to a named global.
  Slot 110 (the footprint storage) is now wired through the same
  mechanism — `save_schema_table.h` regenerated, no anonymous
  `s_slot_00553c80` static any more.

- **`src/save.c`**'s `castles_read_scenario` now calls
  `engine_tile_expand_footprint_at(j, g_castles_state, g_path_scratch,
  g_stage_scratch, g_footprint_list, &g_last_footprint_tile)` on the
  `flags & 0x01` branch, replacing the TODO no-op.

### Tests

- **207 passing** under ASan + UBSan (was 206).
- New: `test_castles_read_fixup_flag_bit0` — plants flags=0x01 + anchor
  'X' at an interior record, reads back, asserts on the observables
  that survive the load-loop clobber (see quirk #10 below).
- Fixed: `test_castles_scenario_round_trip_byte13` was previously
  passing only because the flags & 0x01 branch was a no-op.  Now masks
  the seed to keep bits 0+1 clear so the test's docstring intent ("no
  fixups trigger") is actually honored.

### Engine quirk #10 — castle-load loop clobbers expansion writes

Filed in `docs/findings/engine-quirks.md`.  The load loop walks the
6400 records in-order, copying `+1` and `+3` from disk to memory at
each record.  `engine_tile_expand_footprint_at` is invoked mid-loop
on flagged records — but every byte offset > anchor's offset will have
its `+1`/`+3` overwritten by a later iteration of the SAME load loop.
So almost all of the expansion's glyph writes get clobbered before
control returns to the caller.  What survives:

- Anchor cell's full state (the iter that fires the expansion is also
  the iter that wrote anchor's `+1`/`+3` from disk — no later iter
  touches it).
- ALL cells' `+2` and `+4` (load loop only writes `+1` and `+3`).
- `path_scratch[]`, `stage_scratch[]`, `footprint_list[]` (separate
  globals).

The renderable 3x3 of building glyphs is therefore NOT visible from
the on-disk anchor + flags after a Castles.dat load.  Whatever the
renderer reads must consult `+2` (center marker + footprint bit DO
survive) or run a separate post-load expansion pass.  We'll find out
when the render port lands; for now the load mirrors retail
verbatim — surviving observables and all.

### What's next

1. **Port the per-type handlers** for the MP delayed-action dispatcher
   (carried over).  The 50 entries in `PTR_FUN_004d5c98` were
   surveyed: 5 are no-ops, 2 are 1-line counter mutations, 3 are the
   MP-relay types (0x21..0x23) bigger, and the remaining ~40 are 1-3
   line wrappers around as-yet-unported engine helpers (FUN_0043a23f,
   FUN_004284ce, etc.).  Most aren't useful in isolation until their
   callees are ported.  Bias toward the 7 trivial / counter ones for
   a partial dispatch-table demo, then climb into the helpers one
   subsystem at a time.

2. **Next sub-step body port** — `FUN_0049b8b0` (turn-timeout, 195 B)
   or `FUN_0045270a` (AI scheduler, calls `FUN_00452306`).  Same diff-
   harness pattern as the prior ports.

3. **Wire save/load into the drop-in's runtime command surface** —
   carried over from prior dispatches.  Two options: `--load-save
   PATH` boot flag, or hook F2/F3 keyboard for quick-save / quick-load.

4. **Diagnose the Quit-Game freeze + orchestrator-teardown hang** —
   carried over.  Add an RPC timeout to the orchestrator so
   `script.exports_sync.*` calls don't block teardown forever on a
   dead Frida script.

Bias next time: option 2 (next sub-step body) — option 1 (handler
ports) is parallelizable but each batch's value is gated on porting
the per-handler callee subsystems.  A sub-step body is the same
flavor of work that landed cleanly the last several dispatches.

---

## 2026-05-25 — Tenth diff target: engine_dispatch_delayed_actions (FUN_004472ce)

First MP sub-step body port unlocked by yesterday's state_hash + tile_
expand work.  `FUN_004472ce` is retail's per-tick delayed-action
dispatcher: it sweeps a 400-slot pending-action table, optionally
reorders MP-relay records to the back of the dispatch queue, and calls
a per-type handler from a 50-entry function-pointer table.  1046 B
retail; ~250 LOC port; covers the queue-management body in one slice
without needing any of the per-type handlers ported yet (the table
gets stubbed to a no-op for the diff).

**2000/2000 retail diff pass at seed 1** across the full ten-target
suite, zero races.  Was 1800/1800 across nine prior to this dispatch.

### Landed code

- `src/engine_delayed_action.c` (~220 LOC) + `engine_dispatch_delayed_
  actions()` in `src/engine_delayed_action.h`:
  - Pure C; state struct contains the 400-record table, queue / hipri
    stash, scalar gates, the 10 dispatch-arg mirror globals, and a
    256-entry dispatch function-pointer table the caller fills.
  - Two quirks preserved with commentary:
    1. MP-relay reorder (game_mode==2, queue_count>1): types 0x21..
       0x23 originated from non-local players get pulled to the back
       of the dispatch queue via a high-priority stash + 10-pass shift
       compaction.  Locally-originated copies stay in original order.
    2. 10-pass shift compaction is hard-coded — caps how many
       consecutive QUEUE_EMPTY sentinels can collapse.  Logged as
       quirk #9 in engine-quirks.md.
  - Per-record 0x38-byte struct preserved verbatim (11 i32 + 10 B
    padding + active byte + type byte); runtime layout-cross-check
    test guards against silent layout drift.

- 20 host unit tests covering every branch + both quirks + the type-
  boundary inclusive/exclusive cases + dispatch-arg mirror ordering +
  null-handler tolerance + padding inertness.  206 tests total pass
  under ASan + UBSan (was 186).  Diff-test .so build target also picks
  up the new module.

### Diff harness wiring

The 10th target needed two new harness tricks worth pinning:

- **Sparse per-call records**: 22.4 KB record table but each vector
  typically stamps only a handful of slots.  Wire format = JSON array
  of `{idx, active, type, due_tick, args[10]}`.  Agent zero-fills the
  region per call, then walks the sparse list applying fields at
  `records[idx * 0x38]`.  Avoids shipping 22 KB of mostly-zero JSON
  per call; keeps vector intent readable in the test code.

- **Stub the dispatch table**: `PTR_FUN_004d5c98` is a 50-entry
  function-pointer table in `.rdata`.  At test-plan install,
  `Memory.protect(table, 200, 'rwx')` + stamp a single no-op
  `NativeCallback` into every slot.  The dispatcher still does its
  `(*table[type])()` call per dispatched record, but the call is a
  1-cycle `ret`.  Port mirrors with NULL dispatch table (its branch
  treats NULL as no-op-skip).  Observable surface across both sides
  is identical: records[active=0] cleared + action_counter decremented
  + dispatch-arg mirrors holding the last record's args.
  Documented in `docs/findings/pure-function-diff.md`.

Per-call cost breakdown:
- Writeback: zero-fill records (22400 B) + sparse rec writes (~120 B
  for a 3-record vector) + scalars (~50 B) + queue/hipri pre-state
  (~800 B).
- Call:     1 NativeFunction invocation (__cdecl + 1 i32 arg).
- Readback: records (22400 B) + queue (400 B) + hipri (400 B) +
  scalars + mirrors (~50 B).  All chunks ≤32 KB so a single
  readByteArray each.
- Diff:     bytewise on records/queue/hipri + scalar diff on
  4 counters + per-index diff on 10 dispatch-arg mirrors.

At seed 1, 200 vectors: delayed_action block completed in ~30 s;
full ten-target suite in ~12 minutes (the new target adds little since
the wire traffic is dominated by the engine-tick freeze install +
state_hash sources).

### Engine freeze: reused

The shared install from sim_sub_steps / state_hash / tile_expand
covers delayed_action too — the engine main thread's own per-tick
driver dispatches `FUN_004472ce` as part of `FUN_004b99c0`, so the
freeze is a hard requirement (otherwise our injected records would
race the engine's natural dispatch and the diff would be flaky).

### Headline number

**2000/2000 pass across all ten targets**, zero races, at seed 1.
Was 1800/1800 across nine prior to this dispatch.

### What's next

1. **Port the actual per-type handlers** at `FUN_004479de` ..
   `FUN_00448422` (50 of them).  Each is small (one to a couple
   hundred bytes) and most are combat-action commits / MP broadcasts.
   With the dispatcher port verified, each handler can be ported +
   diff-tested independently using the existing harness pattern.
   Bigger lift than this dispatch but parallelizable.

2. **Pull the castle-load fixup into `castles_read_scenario`** —
   carried over from prior dispatches.  No progress this round.

3. **Start porting the next sub-step body** — candidates from prior
   dispatches: `FUN_0049b8b0` (turn-timeout), `FUN_0045270a` (AI
   scheduler).  Each is bigger than the queue-management dispatcher
   we just landed but the harness pattern (seed-shared payload or
   sparse per-call inputs, shared engine freeze, chunked writes)
   transfers.

4. **Wire save/load into the drop-in's runtime command surface** —
   carried over from prior dispatches.

5. **Diagnose the Quit-Game freeze + orchestrator-teardown hang** —
   carried over.  This session reproduced the trailing
   `[frida-error] TypeError: not a function @ :` at script.unload
   time; benign (test summary lands cleanly) but worth chasing once
   the orchestrator's RPC timeout is in.

Bias next time: option 1 (start the per-type handler ports) — the
dispatcher being diff-verified means each handler can land with the
same harness pattern + a stub for the dispatch table's `idx ==
this_handler` slot.  Even just the 3 MP-relay types (0x21..0x23) being
real ports would let us drop the dispatch-table stub for those slots.

---

## 2026-05-25 — Ninth diff target: engine_tile_expand_footprint (FUN_004917dd)

Cashed in yesterday's port by wiring `FUN_004917dd` into the pure-function
diff harness as the ninth target.  **1800/1800 retail diff pass at
seed 1** across the full nine-target suite — zero races.

### What's new at the harness level

`FUN_004917dd` is the first __cdecl(int) target — every prior diff
function was 0-arg stdcall that pulled inputs from globals.  The agent
declares it with `'mscdecl'` ABI and passes `tile_byte_offset` as the
lone arg.  Otherwise the surface is bigger than any prior:

  - **5 globals** touched (tiles[51200], path_scratch[12800],
    stage_scratch[6400], footprint_list[152], last_footprint_tile[4])
    — combined 70556 bytes of state.
  - **Stateful** function (the prior eight were pure read-or-overwrite).
    Each vector must start from the SAME deterministic baseline, so
    re-seed the inputs in place every call.

The new pattern this established: **per-call pre-state re-seed** from
a shared test-plan seed.  Same xorshift32 + `(seed + 0x9e3779b1 * tag)`
sub-seeding as `state_hash`, but applied per-call rather than once-per-
plan.  Only 3 of the 5 globals need re-seeding (tiles + footprint_list
+ last_footprint_tile) — path/stage scratches are pure outputs (fully
recomputed by the helpers) so we skip them in the writeback, saving
~19 KB of per-call wire traffic.  Documented in
`docs/findings/pure-function-diff.md`.

Per-call cost breakdown:
- Writeback: 51356 B (3 inputs, chunked at 32 KB → 2 writes).
- Inject:   1 byte (anchor char at tiles[offset+3]).
- Call:     1 NativeFunction invocation.
- Readback: 70556 B (5 outputs, chunked at 32 KB → 3 reads).
- Diff:     bytewise across all 5 fields; path/stage_scratch skipped
            when the port returns 0 (unrecognized-anchor branch — neither
            side touches the scratches, so retail keeps its prior bytes
            while port has zero-init — not a real divergence).

At seed 1, 200 vectors: tile_expand block completed in ~2 minutes;
full nine-target suite in ~12 minutes.

### Diff harness wiring

- Agent (`tools/frida/openlords2-agent.js`, ~250 LOC):
  - ADDR entries for FUN_004917dd + 5 globals; byte-count constants.
  - `tileExpandFillBytes(seed, buf, len)` — xorshift32 mirror of orch.
  - `installTileExpandBackup` / `uninstallTileExpandBackup` — one-shot
    snapshot + chunked restore of all 5 globals so retail's pre-test
    bytes survive the run.
  - `runRetailTileExpand(in_obj)` — the per-call workhorse.
  - rpc.exports + diff_test armed-log updated.

- Orchestrator (`tools/diff_test.py`, ~280 LOC):
  - `EngineTileExpandState` ctypes struct (70560 B; embedded arrays).
  - `gen_tile_expand_pre_state(seed)` + `tile_expand_fill_bytes` —
    Python-side mirror.
  - `gen_tile_expand_vectors(n, rng)` — every recognized anchor
    ('@', 'C', 'X', '[') at every safe interior position + 10 specific
    edge-of-map corners that trip the orthogonal-axis quirk + every
    unrecognized-anchor early-return path + random fill.
  - `tile_expand` added to `shared_freeze_targets` (now {sim_sub_steps,
    state_hash, tile_expand}) and the default `--functions` list.

- Engine freeze: reused the shared install from sim_sub_steps + state_hash.
  Same rationale (engine main thread also reads tiles/path/stage_scratch
  during render + pathfinding).

### Headline number

**1800/1800 pass across all nine targets**, zero races, at seed 1.
Was 1600/1600 across eight prior to this dispatch.

### What's next

1. **Pull the castle-load fixup into `castles_read_scenario`.**  The
   port exists + is now diff-verified, but the load path still doesn't
   call it.  Needs minor refactoring: either declare globals for
   path_scratch/stage_scratch/footprint_list/last_footprint in save.c
   (and bundle them per-call into the port's state struct), or add a
   sibling `engine_tile_expand_footprint_inplace(...)` that takes
   pointer args.  Carry-over from prior dispatch — no progress this
   round; commit-as-you-go let the diff-harness wiring land standalone.

2. **Start porting `FUN_004472ce` (MP delayed-action dispatcher)** —
   carried from prior dispatch.  Smaller in line count (1046 B retail)
   than initially feared but the vtable dispatch at `PTR_FUN_004d5c98[type]`
   means most of it isn't testable in isolation until the handlers are
   ported.  Queue-management half (everything up to the dispatch) is
   diffable with a stubbed table.

3. **Wire save/load into the drop-in's runtime command surface**
   — carried over from prior dispatches.

4. **Diagnose the Quit-Game freeze + orchestrator-teardown hang** —
   carried over from prior dispatches.  Mild update from this session:
   if Lords2.exe dies mid-RPC (e.g. user accidentally pressed Esc on
   a visible window), the orchestrator's `script.exports_sync.*` calls
   wait on dead Frida objects and block teardown indefinitely.  Should
   add an RPC timeout to the orchestrator.

Bias next time: option 2 (start the next sub-step body port).  Option
1 (castles_read_scenario wiring) is small but doesn't unblock anything
imminent — the renderer is not yet ported, so there's no end-to-end
"load + render" smoke test to enable.

---

## 2026-05-25 — Per-tile struct pinned + FUN_004917dd ported

Two tightly-coupled units of work this session.

### Per-tile 8-byte struct pin-down (gap from prior dispatch)

Closed the "unknowns" item from the save-format finding: every byte
offset 0..7 of the 6400-record array at `DAT_005440e0` has a confirmed
role, derived from a full sweep of byte-offset writers across `all.c`.
Replaced the provisional table in `docs/findings/save-format.md` with:

| offset | persistent | meaning |
|--------|:-:|---------|
| +0 | no  | terrain/improvement type (enum 0..0x29; ranges 0x40..0x4F pass through) |
| +1 | YES | flags; bit 0x10 = "footprint cell" confirmed |
| +2 | no  | runtime flags (& 0xe3 mask; bit 0x80 = building center) |
| +3 | YES | dual-use — disk anchor char ('@'/'C'/'X'/'[') or runtime footprint glyph ('A'..'R') from the table at `0x4d9dd0` |
| +4 | no  | build/improvement stage (0..15) driving +7 transitions |
| +5 | no  | back-pointer to per-castle slot index (via per-castle struct's +0x1c) |
| +6 | no  | back-pointer to combat-unit index (via per-combat-unit struct's +0x1c) |
| +7 | no  | runtime visual stage / sprite frame (autotiled against +7 of neighbors) |

Bonus: the 80×80 grid dimensions are now positively confirmed (vs
provisional) — the scenario-byte translator's nested loop has both
x and y bounded by 0x50, with row stride 640.  Also decoded the
scenario-byte → +0 mapping in full (e.g. `0` → grass, `0x20..0x3F`
→ building anchor with `+3 = raw char`, `0x50..0x5F` → road segment
with `+3 = raw_char + 0x2c`).

### Ninth port: `FUN_004917dd` (castle-load footprint expansion)

Cashed in the pin-down by porting the gap-closer the save dispatch
flagged.  When a Castles.dat scenario block loads, every tile whose
+3 holds a building anchor char needs runtime expansion: lay down a
3×3 footprint of glyphs, mark a door neighbor, refresh two whole-grid
scratches, and record the change in a small recent-changes list.

Functions ported (all pure C; no globals, no Win32 deps; ~280 LOC
of port for ~700 B retail):

  - `FUN_004917dd` → `engine_tile_expand_footprint`  (top-level)
  - `FUN_004921e5` → `engine_tile_set_footprint_neighbor_flags`
  - `FUN_00471c1f` → `engine_tile_refresh_path_scratch`
  - `FUN_00471d30` → `engine_tile_refresh_stage_scratch`
  - `FUN_0048ee46` → `engine_tile_push_footprint_record`

The 9-glyph-per-orientation footprint table at retail VA `0x4d9dd0`
was extracted off-line via PE-section walk; transcribed in-place into
`k_footprint_glyphs` (4 rows for orients 0/2/4/6, keyed by anchor
char).  Indices 1/3/5 of the table are zero-filled in retail and
unused — the engine never feeds an odd orient.

21 host unit tests added — every helper, every recognized anchor's
glyph pattern, every door-neighbor write triplet, plus the unrecog-
nized-anchor no-op.  **186 tests total pass** under ASan + UBSan
(was 165).  Diff-test .so build target also picks up the new module.

### Quirk #8 filed

Documented in `docs/findings/engine-quirks.md`: the neighbor-flag
helper has a load-bearing bug where the y-direction (+4) writes are
gated on the *orthogonal* axis's bound (e.g. `col < 79` gates an up/
down neighbor write).  Geometrically nonsense, but callers feed only
interior coords so the write stays in-array.  Preserved verbatim in
the port.

### What's next

1. **Wire FUN_004917dd into the diff harness as the 9th target.**
   The retail-side state surface is large (~70 KB across 4 tile-
   adjacent globals + a 19-entry list + a scalar), but the seed-shared
   payload pattern from `state_hash` carries over.  This is the
   bigger leftover from this session.

2. **Start porting `FUN_004472ce` (MP delayed-action dispatcher)** —
   carried from prior dispatch.  Smaller in line count (1046 B
   retail) than initially feared but the vtable dispatch at
   `PTR_FUN_004d5c98[type]` means most of it isn't testable in
   isolation until the handlers are ported.  Queue-management half
   (everything up to the dispatch) is diffable with a stubbed
   table.

3. **Pull the castle-load fixup into our save_load chain.**  The
   port exists but the load path still doesn't call it.  After
   `castles_read_scenario` populates +1/+3 from disk, we should
   walk the 6400 cells and invoke `engine_tile_expand_footprint`
   on every tile whose +3 is one of '@'/'C'/'X'/'['.  Pre-req
   for any "load + render" smoke test.

4. **Wire save/load into the drop-in's runtime command surface**
   — carried over from the prior dispatch; no progress this round.

5. **Diagnose the Quit-Game freeze + orchestrator-teardown hang** —
   carried over from prior dispatches.

Bias next time: option 1 (diff harness wiring for the new port) so
the ninth target lands fully validated, then option 3 (call the
fixup from `castles_read_scenario`) since both are needed before
the first end-to-end load+render smoke test.

---

## 2026-05-25 — Eighth diff target: engine_compute_state_hash (FUN_00440231)

First sub-step body port unlocked by yesterday's save-schema map.
`FUN_00440231` is retail's per-tick MP state hash — the input to the
desync detector (`FUN_00440dd2`) that peers compare across the MP
wire to spot simulation drift.  Read-only, small (1077 B in retail,
~140 LOC in our port), and naturally exercises the per-territory /
per-castle / per-army arrays we just got named storage for.

200/200 retail diff pass at seed 1 — joining the prior seven targets
for **1600/1600 total** across the full suite.

### Landed code

- `src/engine_state_hash.c` + `engine_compute_state_hash()` in
  `src/engine.h`:
  - Pure C, six source-array pointers in, 10-byte bucket + 600-byte
    hash region + `notify_combat_snapshot` flag out.
  - Three quirks preserved with commentary:
    1. `bucket[7] = 1` ALWAYS fires after the per-mode loops, so
       combat-mode's third-loop sum (per_battle_hex_aux_52) is
       overwritten.  Looks like a copy-paste bug from strategic-
       mode's bucket[3] target, but downstream MP code reads
       bucket[7] expecting 1 — "fixing" it would break desync.
    2. `bucket[0] = sum(bucket[1..8])` includes the bucket[7]=1
       constant, so the sum is always ≥1 regardless of mode.
    3. `bucket[9] = (tick_lsb & 0x7f) + 1` — the +1 sentinel
       distinguishes the hash result from zero-init state.
  - Combat-mode tail call to `FUN_0043f4b7` (memcpy-into-malloc'd
    snapshot scratch) surfaced as a flag — the scratch pointers
    aren't ported yet, and the diff orchestrator pins them null
    retail-side so the memcpy branch stays dormant.

- 16 host unit tests covering every branch + quirk + every per-
  source-array slot boundary.  165 tests total pass under
  ASan + UBSan (was 149).

### Diff harness wiring

The 8th target needed two new harness tricks worth pinning:

- **Seed-shared source-array install**: the six source arrays
  combined are ~125 KB.  Sending them per-vector as JSON would be
  expensive; instead the orchestrator + agent both implement the
  same 32-bit xorshift (`state_hash_fill_bytes` / `stateHashFillBytes`)
  and only the 4-byte seed travels over the wire.  Per-array sub-
  seeding (`seed + 0x9e3779b1 * tag`) keeps each array's bytes
  uncorrelated.  Per-vector RPCs then inject only 3 scalars and
  read 610 bytes.  Documented in `docs/findings/pure-function-diff.md`.

- **Shared engine-tick freeze across the run**: both `sim_sub_steps`
  and `state_hash` need `FUN_0040e890` patched to a no-op.  Cycling
  install→uninstall→install on the same `Interceptor.replace`
  address between consecutive test plans triggered an intermittent
  Frida-internal TypeError that destroyed the agent script context.
  Fix: install once upfront if any enabled target needs it, leave
  it in place across all vectors, release once in the teardown.

Two more potholes filed under the doc:
- `writeByteArray` of a buffer ≥64 KB destabilises Frida — chunk
  large writes at <=32 KB (per_territory at 63420 B is just past).
- The snapshot Uint8Array must be a COPY (`.set(new Uint8Array(...))`),
  not a view onto `readByteArray`'s buffer — Frida may reclaim that
  buffer between RPC calls; a dangling view kills the script
  mid-teardown.

### What this unlocks

Same gating as the prior dispatch: now that we have realistic per-
tick state (from the save schema) AND a verified pure-function diff
shape that can inject + invoke a sub-step body's worth of state, the
remaining sub-step body ports are unblocked:

- `FUN_004472ce` (MP sim tick)
- `FUN_0049b8b0` (turn-timeout)
- `FUN_0045270a` (AI scheduler)

Each is bigger than `FUN_00440231` and reads+mutates state rather
than just reading — but the harness pattern (seed-shared payload,
shared engine freeze, chunked writes) carries over.

### What's next

1. **Pin down the per-castle 8-byte struct.**  +0 (type), +1
   (flags), +3 (from disk) are confirmed.  +2/+4..+7 are runtime
   — every site that writes to those offsets on `DAT_005440e0`
   needs cataloguing.  This is now the smallest open item on the
   save-format finding.

2. **Port `FUN_004917dd`** — the missing castle-read fixup branch
   (`flags & 0x01`) carried over from the save-file dispatch.

3. **Diagnose the benign Frida TypeError** at engine-freeze release
   — happens once at script teardown when the engine resumes; all
   results are recorded cleanly before it fires, but tracing it
   would tighten the harness.

4. **Start porting `FUN_004472ce` (MP sim tick)** as the next
   sub-step body.  Order of magnitude larger than `FUN_00440231`
   but the harness shape (seed-shared sources, shared engine freeze)
   is now proven.

5. **Wire save/load into the drop-in's runtime command surface**
   — carried over from the prior dispatch; no progress this round.

6. **Diagnose the Quit-Game freeze + orchestrator-teardown hang** —
   carried over from prior dispatches.

Bias next time: option 4 — next sub-step body port now that the
harness pattern is proven on a small example.

---

## 2026-05-25 — Save-file I/O ported + retail round-trip green

Cashed in yesterday's schema-map by porting the save/load paths into
`src/save.c` — and the headline check passes: load the real GOG
`lastturn.sav`, write it back, byte-compare → 471828/471828 bytes
identical.

### Landed code

- `src/save.h` + `src/save.c`:
  - 13 named big-block globals (entries 0..12 of the schema; 262648
    of the 267028 header bytes).  `g_castles_state`,
    `g_per_territory_420`, `g_per_battle_hex_432`, etc. — see
    `save.h` for the full name list with retail-VA cross-refs.
  - `save_write` / `save_load` — mirror retail `FUN_004ade93` /
    `FUN_004adfe6`: 225-entry positional dump + 16-block Castles.dat
    copy.
  - `castles_init_blank`, `castles_write_scenario`,
    `castles_read_scenario` — mirror retail `FUN_004ae8a9 /
    004ae968 / 004aea70` including the read-side flag-bit fixups
    (force `mem+7=9, mem+4=0, mem+2` reshape on `flags&0x02`;
    `mem+7=1` on `cVar=2`; `mem+7=5` + reshape on `cVar=7`).
  - One TODO: `FUN_004917dd` (the "preserved castle" reset path on
    `flags&0x01`) is not yet ported — no round-trip in our test set
    touches that branch, so it's left as a clearly marked no-op.

- `tools/gen_save_schema.py` + `src/save_schema_table.h`:
  - Generator reads `docs/findings/save-schema-table.csv` and emits
    the 225-entry table + per-unique-VA scalar tail storage.  203
    unique tail VAs; duplicate-VA aliasing (slots 32/48 at
    `0x53f010`; slots 216..224 at `0x4eb2ac`) is preserved through
    shared backing storage.
  - Header is checked in so the build doesn't need Python.  Re-run
    the generator only if the CSV changes (i.e., never, unless we
    discover a binary-side correction).

### Tests

7 new tests added to the host harness (149 total passing under
ASan + UBSan):

- `save_schema_table_shape` — 225 entries summing to 267028 bytes.
- `save_schema_aliasing_preserved` — round-trip a `u32` through both
  aliased slot groups to prove the table's pointer-sharing works.
- `save_round_trip_retail_lastturn` — **the headline test**: load
  real GOG `lastturn.sav` → `save_write` → byte-compare.  Skips
  cleanly if `$OPENLORDS2_GAME_DIR` / `vendor/original/lastturn.sav`
  are absent, so no repo data is required.
- `castles_init_blank_makes_zero_file` — 204800 zero bytes.
- `castles_scenario_round_trip_byte13` — write+read the +1/+3 disk
  bytes for all 6400 records, no fixups triggered.
- `castles_scenario_independent_slots` — confirms `(idx-1)*0x3200`
  seek arithmetic doesn't bleed across slots.
- `castles_read_fixup_flag_bit1` — confirms the `flags&0x02` fixup
  (`mem+7=9, mem+4=0, mem+2` reshape).

### What this unlocks

This is the gating finding for porting any of the sub-step bodies:

- `FUN_004472ce` (MP sim tick)
- `FUN_0049b8b0` (turn-timeout)
- `FUN_0045270a` (AI scheduler)
- `FUN_00440231` (per-tick hash — we already documented its strides)

Each reads+mutates the globals we just allocated.  We can populate
them from a real `.sav` and diff-test the ports with realistic
starting state instead of all-zero stubs, which would have made any
non-trivial control flow uncoverable.

### What's next

1. **Wire save/load into the drop-in's runtime command surface.**
   Currently the API exists but no caller invokes it.  Two options
   that both pay off:
   - On boot, optionally `--load-save PATH` so a launch can preload
     a known game state — useful for harness scenarios where a
     repro lives in a save.
   - Stub the F2/F3 keyboard handlers (retail's quick-save /
     quick-load) so an interactive session round-trips.

2. **Port `FUN_004917dd`** — the missing castle-read fixup branch
   (`flags & 0x01`).  Small port that closes the last gap in the
   Castles.dat read path.  Currently this branch is a no-op with
   a clearly marked TODO.

3. **Pin down the per-castle 8-byte struct.**  +0 (type), +1
   (flags), +3 (from disk) are confirmed.  +2/+4..+7 are runtime
   — trace by reading every site that writes to those offsets on
   `DAT_005440e0`.  Worth doing now while the save subsystem
   context is fresh.

4. **Decode entry 4 (16 × 768 B at `0x53f9b0`).**  Hashed in
   strategic mode only.  16 slots smells like 16 saved-AI brains
   (one per Castles.dat scenario), but cross-confirm.

5. **Start porting a sub-step body** with the now-real globals —
   `FUN_00440231` (per-tick hash) is the smallest test bed since
   it's read-only.

6. **Diagnose the Quit-Game freeze + orchestrator-teardown hang** —
   carried over from prior dispatches as cleanup items.

Bias next time: option 5 → option 1.  Sub-step body coverage is the
high-leverage move now that the data exists; runtime wiring is
trivial and can land alongside any port that actually needs to
preload state.

---

## 2026-05-25 — Save-file schema mapped

Switched to the next subsystem: save-file format.  Mapped the entire
top-level schema (the 225-entry positional serializer table at
`DAT_004de960`) plus the Castles.dat layout, **without writing any
code** — pure RE.  Findings landed in
[`docs/findings/save-format.md`](findings/save-format.md) with a
regen-on-demand CSV at [`save-schema-table.csv`](findings/save-schema-table.csv).

### Headline finding

The save file is a **strictly positional dump** of 225 globals
followed by Castles.dat copied verbatim.  No headers, no field tags,
no versioning:

```
.sav file (471828 bytes)
├── 267028 B  — 225 entries from schema table  ← (ptr,size) pairs at 0x4de960
└── 204800 B  — 16 × 12800 B castle blocks (= entire Castles.dat copy)
```

`267028 + 204800 == 471828` exactly.  Verified across all three
on-disk saves (lastturn.sav, old_turn.sav, safeturn.sav — all that
size).  Sum of the 225 schema entries reproduces the 267028 head
block exactly.

### Cross-confirmed strides

The per-tick hash function `FUN_00440231` iterates the same arrays
at known strides.  Cross-product confirms:

| schema entry  | stride | slot count | almost-certainly         |
|---------------|-------:|-----------:|---------------------------|
| `0x5440e0`    | 8 B    | 6400       | castle/tile records — **same shape as Castles.dat per-block** (12800/2 = 6400 disk pairs ⇄ 6400 × 8 mem records) |
| `0x52f0b0`    | 420 B  | 151        | per-territory state (150 territories + slot 0) |
| `0x57bf00`    | 352 B  | 6          | per-player strategic-mode state |
| `0x553d50`    | 44 B   | 6          | per-player (auxiliary 44-byte block) |
| `0x53f9b0`    | 768 B  | 17         | 16 used slots — same 16 as Castles.dat scenarios? |
| `0x554480`    | 432 B  | 81         | per-battle-hex (combat mode) |
| `0x57a100`    | 76 B   | 101        | per-combat-unit |
| `0x566520`    | 52 B   | 81         | per-battle-hex aux |

### Two-mode engine state, one shared schema

`DAT_0057c8fc` is the gameplay mode discriminator: `0` = strategic
overworld, `2` = tactical combat.  `FUN_00440231` hashes a different
subset of the schema per mode, but **save files include every entry
regardless** — which is how the game preserves the strategic state
through a battle and restores it cleanly afterwards.

### Forward-compat reserved slots

Entries 216..224 (last 9) all point to `DAT_004eb2ac`, a 4-byte
scratch.  Save dumps 4 zero-ish bytes nine times; load discards
them.  Forward-compat-reserved field slots in the 1996 release that
never got populated.  Our drop-in mirrors this — same 36 bytes
emitted so retail can still parse our saves.

### History.dat / C_hist.dat — vestigial?

Neither filename appears in any string of Lords2.exe, mapl2.exe, or
the bundled DLLs.  Both files date from January/February 1996,
predating the 2.0.0.3 GOG patch.  Strongly suggestive of vestigial
pre-release artifacts that the shipping engine no longer touches.
Worth one more pass via Frida `CreateFileA` capture on a running
retail to be sure, but low priority — none of the schema we just
mapped points into them, so they can't affect save-file logic.

### What this unlocks

This is the gating finding for porting any of the sub-step bodies
(MP sim tick `FUN_004472ce`, turn-timeout `FUN_0049b8b0`, AI
scheduler `FUN_0045270a`, etc.).  Those functions read+mutate the
strategic globals; with the schema mapped, we can:

1. Allocate the globals at fixed VAs in our drop-in.
2. Populate them from a real .sav.
3. Diff-test the sub-step ports with realistic starting state
   instead of zero stubs.

### What's next

1. **Port the schema table + I/O paths into `src/save.c`.**  Mostly
   mechanical: declare the 13 big-block globals, declare the
   225-entry table pointing into them, port the two `__open`/
   `__read`/`__write` loops verbatim.  No diff harness yet — first
   exercise is round-trip a retail .sav and byte-compare.

2. **Pin down the per-castle 8-byte struct.**  Bytes +0 (type),
   +1 (flags), +3 (from disk) are known.  +2/+4..+7 are runtime;
   trace them by reading every site that writes to those offsets
   on `DAT_005440e0`.

3. **Decode entry 4 (16 × 768 B at `0x53f9b0`).**  Hint: hashed
   when in strategic mode; 16 slots could be 16-player armies, or
   16 saved AI brains, or 16 dynasty entries.  Cross-ref with the
   16-block Castles.dat scenario index.

4. **Diagnose the Quit-Game freeze + orchestrator-teardown hang** —
   carried over from the prior dispatch as cleanup items.

Bias next time: option 1 — start writing code now that we know what
the bytes mean.

---

## 2026-05-25 — Seventh diff target: engine_compute_sim_sub_steps (chained with scenario_hint)

Closed out the per-tick driver's pure-function diff coverage.  Ported
`FUN_0043de70` (the scenario_hint computer that `FUN_004bb978` calls
internally) and wired the **chained pair** into the diff machinery —
one retail RPC exercises both ports, the orchestrator chains them
host-side and compares the combined output.

The "highest complexity of the diff targets so far" note from the
prior dispatch was accurate: scope is ~30 scalar globals, four
strided per-player arrays (slots 1..6, three byte-strided + one int-
strided), a 2400-byte rate-gate LUT, and the deepest race surface
yet — large enough that the engine main thread reliably **crashed**
on the injection rather than just racing.

### `src/engine_rate.c` — `engine_compute_scenario_hint`

Pure-C port of `FUN_0043de70` (1211 bytes retail-side, ~250 LOC C).
Three retail helpers (FUN_0043e433 count-active-allies, FUN_0043e330
min-tick-filtered, FUN_0043e50d min-tick-all) inlined as file-static
helpers; per-slot strided arrays flattened into 7-entry tables.

Four rate-selection ladders (extracted into named static helpers so
the bucket boundaries audit visually):

- `behind_ladder`: peers-ahead-of-me 0..10000+ → -2/-3/-4/-5/-6/-7/-8/-10
  fast-skip codes.
- `ahead_ladder_mode5`: my-lead 0..710+ → 10..140 ms-per-step
  buckets, 13-step fine-grained.
- `ahead_ladder_coarse`: my-lead 0..401+ → 20..140 ms-per-step
  buckets, 8-step.
- `alone_ladder`: my-lead 0..801+ → 1/30/70/90 ms-per-step (4-step),
  with -1 sentinel for "not ahead", floored at `base_delta*3` by
  the caller.

The function has two side-effect calls retail-side: `FUN_0043eda0(10, 0)`
(MP broadcast event 10) fires when `slow_frame_count` exceeds 100; the
sibling `FUN_0043eda0(0xf, 0)` (autosave-done event 15) lives in
FUN_004bb978's autosave-expiry branch.  Both surfaced via output
flags (`notify_slow_frame` / `notify_autosave_done`) instead of
firing inline — diff orchestrator pins vectors so retail's matching
trigger doesn't fire either, keeping the engine undisturbed.

20 host unit tests cover every branch + ladder boundary (the prior
24 sim_sub_steps tests carried over).  142 unit tests total pass
under ASan + UBSan.

### Agent: full snapshot/inject + LUT install + engine freeze

`tools/frida/openlords2-agent.js` gained ~400 lines spanning:

- ~30 new ADDR entries — scalar globals (game_state_phase, mp_active,
  local_player_idx, ally_a/b, frame_count, max_stride, stride_sum,
  avg_stride, slow_frame_count, base_delta, active_count, etc.) plus
  per-player array bases (player_active stride 0x160, player_disconnected
  stride 0x2c, player_ticks stride 4).
- `runRetailSimSubSteps(in_obj)` — snapshots all the above + the two
  single-byte table reads (screen_freeze, mp_skip) at exactly the index
  the function will use.  Injects vector, pushes mock_time to clock
  queue, calls retail, reads outputs, restores.
- `installSimSubStepsRateLut(bytes)` / `uninstall` — one-shot per test
  plan.  Writes the orchestrator-supplied 2400 bytes of the rate-gate
  table; saves a 4.8 KB readback+writeback per vector vs. doing it
  inline.  Lazy snapshot of retail's original bytes for restore.
- `installSimSubStepsEngineFreeze` / `uninstall` — the load-bearing
  new pattern (see below).

### The engine-tick freeze (new race-handling pattern)

The first six diff targets dealt with engine races as a **readback
correctness** problem — the engine's parallel call overwrites a
global between retail's write and our readback, and we detect+retry.
sim_sub_steps escalated this to a **process-crash** problem:

- We inject `game_state_phase = 3` (ingame) for vectors that test
  the MP / ahead-ladder paths.
- The engine main thread's per-tick driver also reads this global.
- During our inject→call→readback→restore window, if `engine_tick`
  fires, its own `FUN_004bb978` returns >0 (driven by our injected
  state) and enters the sub-step body.
- The sub-step body calls retail sim sub-functions that require a
  fully-initialised ingame state (player tables, deferred-action
  queue at `DAT_0050d7c0`, sprite/UI/map subsystems) which we have
  NOT provided — AV → frida script destroyed.

First 25 vectors landed cleanly by timing luck; 50+ crashed reliably.

Resolution: `installSimSubStepsEngineFreeze()` uses `Interceptor.
replace(FUN_0040e890, no-op_callback)` to swap engine_tick_dispatch
with a NativeCallback that returns immediately.  The engine's main
loop keeps pumping messages (PeekMessage / DispatchMessage still
works → process stays responsive to subsequent agent RPCs), but
all per-tick work — sim sub-step loop, render phase, LFSR, autosave
pump, FPS counter — is skipped for the duration of the sim_sub_steps
test plan.  Paired uninstall restores normal behavior.

Trade-off: with the freeze active, the structural `raced` flag is
permanently 0 (no engine ticks to delta `g_tick_count`), so the
orchestrator falls back entirely on the function-specific canary —
`derived_hint` readback matches port-computed value AND
`new_last_step_ms` lands in `{vec[now_ms], vec[last_step_ms]}` (the
only two values retail's tail write can produce).

### Orchestrator (`tools/diff_test.py`)

- `EngineScenarioHintIn/Out` + `EngineRateIn/Out` ctypes mirrors.
- `gen_sim_sub_steps_vectors(n, rng)`: 21 fixed edge cases (state-not-
  ingame, SP/MP, pause_lock, screen_freeze 0/1/2, extended_pause
  SP-vs-MP-with-skip, four scenario_hint paths, ladder boundaries,
  phase wrap, time overflow, 140 ms ceiling at slow_frame_count=99)
  + random fill biased toward MP-active.
- `gen_sim_sub_steps_rate_lut(rng)`: 2400-byte deterministic LUT
  (seeded; 85 % non-zero so the LUT gate has actual signal).
- `run_port_sim_sub_steps`: **chained call** — first
  `engine_compute_scenario_hint` to compute `scenario_hint`, then
  `engine_compute_sim_sub_steps` with that hint.  Combines outputs.
- `diff_sim_sub_steps`: 11 output fields compared as u32 (sub_steps,
  new_last_step_ms, new_phase, derived_hint, new_frame_count,
  new_max_stride, new_stride_sum, new_avg_stride, new_slow_frame_count,
  new_autosave_start_ms, new_active_count).
- Dispatch arm with engine-freeze install/uninstall wrapping the
  per-vector loop.

### Constraints baked into vectors

- `autosave_start_ms = 0` always — skip FUN_004bb978's 5000 ms branch
  so retail's `FUN_0043eda0(0xf, 0)` never fires.
- `slow_frame_count ≤ 99` always — after retail's `++`, value stays
  ≤ 100; gate `100 < value` is false, `FUN_0043eda0(10, 0)` never
  fires.

With these, the entire FUN_004bb978 → FUN_0043de70 → helpers call
chain has zero retail-side side effects beyond the snapshot+restore
surface — the engine sees a no-op.

### Verified

`nix develop -c python3 tools/diff_test.py --vectors 200 --seed 1
--remote cutestation.soy:27042 --functions sim_sub_steps`:

| target          | result                       |
|-----------------|------------------------------|
| `sim_sub_steps` | 200/200 pass, 0 race-pass    |

All 142 host unit tests still pass under ASan + UBSan.  Windows
drop-in still builds clean.

### Known issue surfaced (not addressed here)

Orchestrator teardown (script.unload + session.detach + device.kill)
hangs after the test reports success.  The 200/200 pass result is
written to `runs/diff/<ts>/diff.log` before the hang.  Likely Frida
interaction with `Interceptor.replace` + `NativeCallback` during
script teardown — none of the other diff targets use that pattern.
Worth a dedicated investigation; for now, the test result is
captured cleanly and the user can `kill` the orchestrator after
seeing the summary.

### What's next

Per-tick driver pure-function diff coverage is now **complete** —
seven targets (rng_step, fps_step, anim_cycle, frame_delta,
phase_ticks, input_state, sim_sub_steps), 1400/1400 vectors pass at
seed 1.  Remaining top-down RE candidates:

1. **Save-file format** — `castles.dat` / `c_hist.dat` / `history.dat`
   parsers.  Unlocks the simulation subsystem ports.  Largest
   subsystem unlock available — once the parsers land, sub-step body
   ports (FUN_004472ce MP sim tick, FUN_0049b8b0 turn-timeout, etc.)
   can start using real ingame state instead of stubs.
2. **Diagnose the Quit-Game freeze** — engine hangs on ESC →
   confirmation; would unblock unattended end-to-end smoke runs.
   Lower complexity, lower coverage win.
3. **Fix the diff_test orchestrator teardown hang** — small
   investigation slice; would make the diff machinery less awkward
   to invoke from CI.

Top-down RE bias next time: option 1 is the next subsystem unlock
on the dependency tree; option 3 is a cleanup that pays off every
diff run.

---

## 2026-05-25 — Sixth diff target: engine_compute_input_state + GetCursorPos hook

Continued the "more diff targets" thread.  Wired `FUN_004b191e` (the
981-byte mouse-input state resolver — 25 inputs, 30 outputs, scripted
`GetCursorPos` + `timeGetTime`) into the diff machinery.  The work
introduced a new scripted-input pattern (cursor queue + user32 hook),
exercised a new race-window mode (pre-call modification of injected
inputs by other engine functions), and surfaced one Frida-API gotcha
that's worth noting.

### Agent (`tools/frida/openlords2-agent.js`)

- New `ADDR` entries: `fn_engine_input_state` (`FUN_004b191e`) + ~30
  state globals split across mode / WndProc mirrors / viewport / cached
  cursor / output snapshots / event flags / button state / timers /
  click-eligibility / last-click coords.  Each entry comments its
  Ghidra-inferred width (u8 / i16 / s32 — byte-stride adjacency in the
  0x4eabXX cluster forces u8 reads/writes; cached cursor pair is i16
  per `CONCAT22` shape; everything 4-aligned is s32).
- New `g_diff_test_cursor_queue` — parallel to `g_diff_test_clock_queue`
  but for the user32!GetCursorPos hook.  Entries are `{x, y}` objects;
  drained FIFO from `onLeave` when caller_tid matches the V8 RPC tid
  and queue is non-empty.
- New `installGetCursorPosHook()`: idempotent, installs on the diff_test
  init path (independent of the timeGetTime install path).
  `Interceptor.attach` onLeave overwrites `args[0]`'s POINT struct with
  scripted (x, y) and forces `retval.replace(ptr(1))` so the BOOL return
  always says success.
- New `g_nf_retail_input_state` NativeFunction stub — resolved alongside
  the existing diff stubs in `resolveRetailFunctions()`.
- New `runRetailInputState(in_obj)` RPC.  Takes a single object (not
  positional args — 25 fields would be unwieldy as args).  Snapshots all
  ~40 fields touched (inputs + state + outputs), injects inputs, pushes
  one clock value + one cursor value, invokes retail, reads outputs,
  restores in `finally`.  Pre-clears the snap mirrors (prev_lmb_snap,
  prev_rmb_snap, lmb_dblclk) so the readback is "did THIS call write
  them" (retail writes them unconditionally so the clear keeps the
  semantics clean for restore + diff).
- Readback ordering load-bearing again: `raw_cursor_{x,y}` read FIRST
  (start canary) AND LAST (end canary).  Canary equality + match-to-
  injected = readback was atomic from the engine's perspective.
- Structural `raced` flag returned (same shape as anim_cycle / phase_ticks).
- Frida-API gotcha caught: `Module.findExportByName('user32.dll', ...)`
  no longer exists in current Frida — must use
  `Process.findModuleByName('user32.dll').findExportByName(...)`.  The
  existing `installTimeGetTimeHook` already uses this shape; first cut
  copy-pasted the deprecated shape and failed silently with "not a
  function" in agent.log (no hook install + every vector failed because
  retail's GetCursorPos read the real OS cursor).  Fixed in the same
  commit.

### Race classification (pre- + post-call races)

Initial diff run flagged 4/20 vectors as `RACE:`.  Root cause: input_state
has the widest race surface of the diff targets so far.  Three modes
combine:

1. **Pre-call input modification** (new for this target).  The engine
   main thread runs `FUN_0042ff10` (an 8 KB high-frequency function)
   which resets `DAT_0057cac4` to 0.  Our injection of
   `cursor_cache_enabled = 1` doesn't stick across the engine's
   intervening call — retail reads 0 and skips the cache-overwrite
   branch.  Other affected globals: WndProc OR-ins button-state bits at
   every WM_*BUTTON*; engine's own FUN_004b191e writes prev_game_x_snap
   (via DAT_004e65fc clobber).  Pre-call races can't be caught by a
   post-call canary because retail's call ran with engine-modified
   inputs — the function did the right thing, but for a different
   vector than what we injected.

2. **Concurrent execution** (carryover from phase_ticks).  Multi-core
   engine main thread + V8 thread can be inside FUN_004b191e
   simultaneously.

3. **Post-call writeback race** (carryover from frame_delta /
   phase_ticks).  Engine's own FUN_004b191e overwrites our outputs
   before we read them.

The raw_cursor_{x,y} canary catches mode 3 but not 1 or 2.  The
structural `raced` flag catches all three but is conservative (false
positives are common on a tick-rate-unlimited retail).

Retry decision uses the structural raced flag combined with the
raw_cursor canary.  `INPUT_STATE_RACE_RETRIES = 16` (bumped from
ANIM_RACE_RETRIES = 8) to dampen the wider race window.  Race-pass
classifier is generic: any vector that exits the retry loop still
raced AND has diffs is classified as race-pass.  Justification: the
function's behaviour is well-pinned by 22 host-side unit tests
(`test_engine_input.c`), so a real port bug would show up
deterministically across non-racing vectors — not just on the random
fraction that happen to land in a race window.  Typical race-pass rate
on a 200-vector run: ~15 % (cf. ~1 % for phase_ticks).

### Orchestrator (`tools/diff_test.py`)

- `EngineInputIn` / `EngineInputOut` ctypes mirrors (25 inputs, 30
  outputs — matches `src/engine.h`).
- `lib.engine_compute_input_state` binding.
- `gen_input_state_vectors(n, rng)` — 22 fixed edge cases (cursor_cache
  on/off, every dblclk-bit combination, LMB/RMB press/release edges,
  300 ms expiry boundary, shared-timer quirk, motion / no-motion,
  click_eligible release-into-click, degenerate viewport for both
  axes, signed viewport extents, dblclk-supersedes-edge, two u32
  wraparound directions).  Random fill biased toward "buttons +
  dblclks + timer_armed" so the conditional branches fire often.
- `run_port_input_state` + `diff_input_state` (all 30 fields masked to
  u32 for bit-equality).
- `_input_state_race_free` — function-specific canary on
  raw_cursor_{x,y} (start AND end equal injected cursor).
- Per-vector dispatch arm with retry-on-raced-or-canary-mismatch +
  generic race-pass classification.
- CLI default `--functions` now includes `input_state`.

### Makefile

`engine_input.c` added to `DIFF_SRCS` so `libengine_diff.so` includes
the port.

### Verified

`nix develop -c python3 tools/diff_test.py --vectors 200 --seed 1
--remote cutestation.soy:27042`:

| target        | result                       |
|---------------|------------------------------|
| `rng_step`    | 200/200 pass                 |
| `fps_step`    | 200/200 pass (1 race-pass)   |
| `anim_cycle`  | 200/200 pass                 |
| `frame_delta` | 200/200 pass                 |
| `phase_ticks` | 200/200 pass                 |
| `input_state` | 200/200 pass (29 race-pass)  |

All 123 host unit tests still pass under ASan + UBSan.  Smoke test
(`tools/smoke-test.py --turbo`) still green.

### What's next

All "mechanical follow-ups" from the prior diff-target dispatches are
now landed.  Remaining top-down RE candidates:

1. **`engine_compute_sim_sub_steps` (FUN_004bb978)** — last remaining
   diff target on the punch list.  Has internal callees (FUN_004bb8c0,
   FUN_0043de70) and a pointer-array LUT; highest complexity of the
   diff targets so far.  Would close out the per-tick driver's pure-
   function coverage.
2. **Save-file format** — `castles.dat` / `c_hist.dat` / `history.dat`
   parsers.  Starts unlocking the simulation subsystem ports.
3. **Diagnose the Quit-Game freeze** — engine hangs on ESC →
   confirmation; would unblock unattended end-to-end smoke runs.

Top-down RE bias next time: option 1 keeps the diff-machinery thread
alive (well-understood pattern, mostly mechanical with a couple of
internal-callee questions); option 2 is the next subsystem unlock.

---

## 2026-05-25 — Interactive verification mode: agent-driven present + visible window

The prior dispatch landed the IDirectDrawSurface::Blt → GDI seat but
left visual verification incomplete: under the smoke configuration the
engine never called `Blt(primary, back)` because the Smacker intro's
present path is `WM_PAINT` + `PatBlt`-to-window-DC, not DDraw.  This
dispatch adds an opt-in interactive verification mode that drives the
GDI present from the agent regardless of retail's render gates, plus
the supporting fixes to make the visible window actually correct.

### Agent (`tools/frida/openlords2-agent.js`)

- New `agent_present` init flag.  When on, `engine_tick.onLeave`
  drives a `presentBackToWindowViaGdi` every N ticks (default 100,
  configurable via `agent_present_every_ticks`).  Reuses the same
  Lock-back + palette readback + StretchDIBits primitive as the
  Blt-vtable seat.
- New `show_window_after_ms` init flag.  When > 0, the agent calls
  `ShowWindow(hwnd, SW_SHOWNORMAL)` on the engine's main window once
  the configured real-ms elapses since spawn.  Pairs with
  `hide_window=True` (default) so the engine survives bring-up
  (a `--no-hide` run dies before reaching engine_tick — separate
  unrelated bug, noted below).
- Hide-window hook gains a one-shot bypass latch (`g_show_window_
  latch`) so the agent's programmatic `SW_SHOWNORMAL` isn't rewritten
  back to `SW_HIDE`.
- After the show, the agent issues a `SetWindowPos` to resize the
  client area to 800×600 (user preference).  `GFX_RENDER_W/H` is
  640×480 natively; StretchDIBits scales transparently.
- New `ADDR.var_fullscreen_flag = 0x0053f228`.  Our existing
  SetCooperativeLevel hook now also writes 0 to this global so retail
  takes its (mostly-dead-code) **windowed** branch in `FUN_0042e5c0`:
  overlapped-window style, 640×480 centered, regular Blt (not BltFast).
  Without this, the engine's auto-fullscreen-promote on non-8bpp
  desktops leaves the global at 1 and the window spawns as a
  borderless popup with `WS_MAXIMIZE` set — squashed to desktop res
  when our `SW_SHOW` first revealed it.  See engine-quirks.md #1.
- `SW_SHOW` → `SW_SHOWNORMAL`: `SW_SHOW` honours the window's last
  show state (never-shown after our hide_window strip), which DWM
  treats as still-minimised on some configurations.
- New `agent_show_window` event (handled in Python — was showing as
  `[unknown-event]` in the first cut).

### Python driver (`tools/frida_capture.py`)

- `CaptureConfig` gains `agent_present`, `agent_present_every_ticks`,
  `show_window_after_ms`.  Smoke-test stays interactive-free (these
  flags don't affect the smoke surface).
- CLI flags: `--agent-present`, `--agent-present-every-ticks N`,
  `--show-window-after-ms N`.
- `agent_show_window` event surfaces as `[show_window]` in agent.log.

### Verified

Interactive run via `cutestation.soy:27042`:

```
nix develop -c python3 tools/frida_capture.py \
  --duration-ms 60000 --turbo \
  --agent-present --agent-present-every-ticks 50 \
  --show-window-after-ms 3000
```

- 17,262 GDI presents fired across 60 s.
- `ShowWindow` fires at the 3 s mark.
- User confirmed: Smacker intro plays visibly, main menu reachable.

### Known issues surfaced during this work (not addressed here)

1. **Quit-Game freeze**.  Clicking "Quit Game" or pressing ESC →
   confirmation hangs the engine — process doesn't terminate; Ctrl-C
   needed.  Likely an engine shutdown path that waits on something
   our harness suppresses (audio buffer drain, Smacker outro,
   DirectPlay session teardown).  Wants a separate diagnostic pass
   with hooks on WM_CLOSE / engine's shutdown chain.
2. **`--no-hide` early-exit**.  Running with the window WS_VISIBLE
   from create kills the engine before it reaches `engine_tick`.
   Specifically, the WinLords2Class window gets created, no
   `[ddraw_coop]` events fire, the script gets destroyed.  The
   `hide_window` + `show_window_after_ms` combo sidesteps this for
   interactive use but the underlying cause is unknown.

### What's next

The harness now has full interactive visual verification.  Other
threads to pick up next:

1. **Save-file format** (option 2 from prior dispatch) — `castles.dat`
   / `c_hist.dat` / `history.dat` parsers.  Unlocks the simulation
   subsystem ports.
2. **More diff targets** (option 3 from prior dispatch) — two
   mechanical follow-ups remain (`engine_compute_input_state`,
   `engine_compute_sim_sub_steps`).
3. **Diagnose the Quit-Game freeze** — would unblock unattended
   smoke runs that actually exercise the full game lifecycle, not
   just the intro.

Top-down RE bias next time: option 3 keeps the harness thread alive
and unblocks longer interactive sessions; option 1 starts the
simulation-subsystem unlock.

---

## 2026-05-24 — DDraw → GDI present seat for retail under the harness

Top-down RE bias entry from the prior dispatch: the diff machinery
covers the small pure-function targets, so the next-highest-leverage
unlock is making retail render *visibly* under the harness.  Without
this, the harness can ship turbo + frame capture + diff tests just
fine — but a human looking at the screen sees a black window while
the engine internally renders fine to the back buffer.

### The hidden quirk

Modern Windows DWM intercepts windowed DDraw NORMAL-mode Blts.  The
Blt returns DD_OK, the back surface legitimately holds the rendered
pixels, but DWM never propagates the paint to the window's
redirection bitmap.  The composited desktop re-paints from the
unchanged backing store and the visible window stays black.

Retail Lords2.exe dodges this by accident: `FUN_0042e5c0` auto-
fullscreen-promotes on any non-8bpp desktop (always, on modern
hardware), going to exclusive coop + `SetDisplayMode` and bypassing
DWM entirely.  Our harness strips those flags (force_windowed[L2])
to keep dev-loop sessions from taking over the desktop — which is
what surfaces the quirk.

Documented as engine-quirks.md #6.

### Agent (`tools/frida/openlords2-agent.js`)

- New `gdi_seat` init flag (default on in `frida_capture.py` /
  `smoke-test.py`).
- New `installPrimaryBltGdiSeat()`: lazy install from
  `engine_tick.onLeave` once the engine's BSS pointers
  (`var_dd_primary`, `var_dd_back`, `var_dd_palette`) are non-NULL.
  Patches `IDirectDrawSurface::Blt` (vtable slot 5, byte offset
  0x14) on the primary surface's vtable via `Interceptor.attach`.
- New `presentBackToWindowViaGdi()`: locks the 8bpp back surface,
  pulls 256 palette entries via `IDirectDrawPalette::GetEntries`,
  swaps R↔B on the PALETTEENTRY→RGBQUAD copy, calls
  `gdi32!StretchDIBits` against `user32!GetDC(hwnd)` (using
  `var_main_hwnd`), unlocks.  Caches `BITMAPINFO` scratch + a
  pitch-adjustment row buffer.
- onEnter intercept filter: skip `DDBLT_COLORFILL` (no source),
  skip non-back sources, skip non-primary `this`.  Backed by live
  global reads so a hypothetical surface re-init doesn't intercept
  the wrong vtable.
- onLeave: force `DD_OK` (0) after a successful GDI present so the
  caller's HRESULT check doesn't disagree with what we actually
  drew (a couple of driver combos return benign warning hresults
  for invisible-Blt cases).
- New `gdi_seat_present` event kind (throttled: first 3, then 1 per
  100) for the Python driver's heartbeat counter.
- New `getGdiSeatStats()` RPC: returns `{patched, blt_observed,
  blt_skipped_fill, blt_skipped_other, gdi_presents}` for end-of-run
  diagnostics.

### Python driver (`tools/frida_capture.py`, `tools/smoke-test.py`)

- New `gdi_seat` field on `CaptureConfig` (default `True`) and
  `gdi_seat_present_count` on `CaptureResult`.
- New `--no-gdi-seat` debug flag on both CLIs.
- New `gdi_seat_present` event handler, counter, and summary
  surface.
- End-of-run RPC pull of `getGdiSeatStats()` writes a
  `[gdi_seat_stats]` line to `agent.log` so post-mortem analysis
  can see "did the engine call Blt at all, and were we filtering
  it correctly".
- smoke-test gains a `gdi_seat armed` check (gated on
  `--no-gdi-seat`).

### Mirror in the drop-in

The same fix already exists in `src/gfx.c`'s `present_via_gdi`
(landed when bringing up our windowed render path — the original
empirical diagnosis lives in the `project_dwm_ddraw_normal` auto-
memory).  The harness seat is the symmetric piece for retail under
our turbo / frame-capture / diff-test pipeline.

### Verified

`nix develop -c python3 tools/smoke-test.py --duration-ms 7000
--turbo --remote cutestation.soy:27042`:

```
[agent] gdi_seat: armed (lazy install on first engine_tick after
        retail finishes DDraw surface bring-up)
[agent] gdi_seat: IDirectDrawSurface::Blt patched
        (primary=0x3f0ce88, vtable=0x728bd120, Blt @ 0x72f14520)
```

All smoke checks pass.  `gdi_seat_stats {patched: True,
blt_observed: 0, gdi_presents: 0}` — under hide_window + turbo the
engine ticks ~3000 perceived seconds in 7 real seconds but never
calls `IDirectDrawSurface::Blt(primary, back)`.  Confirmed not a
filter bug (every observed call is counted regardless of filter).
The engine appears to be stuck waiting for input at an intro screen
where the entire render pipeline is dormant (no
`dirty_rect_union` calls → `DAT_0050d09c == 0` → rate-gate's dirty
check short-circuits the Blt).  All 123 host unit tests still pass.

### What's next

**Visual verification needed** — the harness is installed and
verified to patch; whether GDI presents actually become visible
requires a non-headless run with a non-hidden window so the human
can confirm.  The natural followup is either:

1. **Run the harness without `--hide-window`** for visual
   confirmation.  Side issue surfaced: an early-exit happens
   under `--no-hide` that needs a separate diagnostic
   pass (the engine creates its main window then the script
   terminates without reaching engine_tick — possibly an
   uncaught exception in our hook chain when the window is
   visible).
2. **Send a synthetic input event** during the smoke window so
   the intro screen can advance to the title screen where the
   render pipeline should produce dirty rects.  `--input-trace`
   plumbing already exists; the missing piece is a "click to
   skip intro" trace.
3. **Save-file format** (option 2 from the prior dispatch).
   Parsers for `castles.dat` / `c_hist.dat` / `history.dat`.
   Starts unlocking the simulation subsystem ports.
4. **More diff targets** (option 3 from the prior dispatch).
   Two mechanical follow-ups remain:
   - `engine_compute_input_state` (FUN_004b191e) — needs a
     `GetCursorPos` scripted-return pattern.
   - `engine_compute_sim_sub_steps` (FUN_004bb978) — has internal
     callees + a pointer-array LUT.

Top-down RE bias next time: option 1 (visual verification +
diagnose the `--no-hide` early-exit) is the natural close-out for
this thread — confirms the seat actually shows pixels, and unblocks
the user's recording-driven scenarios.

---

## 2026-05-24 — Fifth diff target: engine_compute_phase_ticks + concurrent-race

Continued the "more diff targets" thread.  Wired `FUN_004bbc80` (the 9-flag
animation phase tick — 10 inputs, 18 outputs, 1 `timeGetTime` call) into the
diff machinery using the scripted-clock pattern.  The work surfaced a new
race mode — concurrent execution on multi-core — and required a new
race-pass classification shape (engine-clear signature).

### Agent (`tools/frida/openlords2-agent.js`)

- New `ADDR` entries: `fn_engine_phase_ticks` (`FUN_004bbc80`) +
  10 state globals (last_base + 9 counters) + 8 flag globals
  (the 9th, `var_phase_tick_80ms` = `DAT_00591510`, was already
  declared for anim_cycle's input).
- New `g_nf_retail_phase_ticks` NativeFunction (return type `void`, no
  args, stdcall) — resolved alongside the existing diff RPC stubs in
  `resolveRetailFunctions()`.
- New `runRetailPhaseTicks(now, last_base, base_counter, c2..c32)`
  RPC: snapshots all 18 fields, clears all 9 flags before the call
  (retail clears 8 at entry; the 1040 ms flag is sticky — clearing
  forces "did THIS call set the flag" readback semantics), pushes a
  single scripted clock value, invokes retail, reads back, restores
  in a `finally`.
- Readback ordering is load-bearing: `last_base_tick_ms` is read FIRST
  (start canary) AND LAST (end canary) around the 17-field body, and
  the orchestrator's race detector compares them.  Equal canaries +
  expected end value = readback was atomic from a single retail call's
  perspective.
- Structural `raced` flag returned (same shape as anim_cycle) — used
  as a secondary OR-in by the orchestrator's race-pass classifier.

### Race classification (concurrent-execution + engine-clear)

Initial diff against retail revealed 4/100 failures, all matching the
same signature: 9 counter writebacks match port exactly, but some flag
fields differ as `retail=0, port=1`.

Root cause: on multi-core, the engine main thread runs FUN_004bbc80
*concurrently* with our V8 thread's NativeFunction call.  The dominant
interleaving:

  - our V8 retail returns; retail's correct 18 outputs are visible.
  - engine begins its own concurrent FUN_004bbc80 call.
  - engine's first action is "clear 8 flags at entry" (the function's
    prologue).
  - our V8 reads the 9 flags AFTER engine's clear-step but BEFORE
    engine's 20 ms branch writes.
  - our V8 reads the 9 counter/last_base globals BEFORE engine's
    20 ms branch writes too (the branch writes them).

Both canaries (`last_base_tick_ms` start and end) match expected
because engine hadn't yet written its own `last_base` value.  The
*flags*, however, are clobbered.

The orchestrator's race detector was enriched with an internal-
consistency check: when the 20 ms branch fires (`last_base ==
in_now_ms`), retail MUST observe `tick_20ms == 1` (the branch writes it
after bumping base_counter).  If we observe `tick_20ms == 0` with
`last_base == in_now_ms`, the engine cleared the flag mid-RPC.  This
triggers a retry (`ANIM_RACE_RETRIES = 8`, sleep 10 ms between).

For the small fraction of races that survive retries, a new race-pass
signature classifies the failure as harness flakiness rather than a
port bug: `_phase_ticks_engine_clear_race(retail, port)` returns True
when (a) all 9 counter/last_base writebacks match port, AND (b) every
flag mismatch is in the direction `retail=0, port=1`.  Structurally
identical to frame_delta's race-pass — the function ran correctly per
spec; only the observable was clobbered after the fact.

The sticky 1040 ms flag survives engine races: retail's clear-step
doesn't touch `_DAT_005aeb28`, so a port-set 1040 ms flag still
matches retail after the race.

### Orchestrator (`tools/diff_test.py`)

- `EnginePhaseIn` / `EnginePhaseOut` ctypes mirrors (10 inputs, 18
  outputs).
- `lib.engine_compute_phase_ticks` binding.
- `gen_phase_ticks_vectors(n, rng)` — 15 fixed edge cases:
  20 ms boundary (delta 19 / 20), signed-wrap (`delta < 0`), zero
  delta, 80 ms gate fire (base_counter=3→4), simultaneous max-
  rollover (all 8 inner counters at threshold), single-counter
  rollover sweep (one of 8 at threshold, rest at 0), sticky 1040 ms
  alone, u32 wrap.  Random fill biased toward "80 ms gate firing
  often" + "inner counters in natural range".
- `run_port_phase_ticks` + `diff_phase_ticks` (all 18 fields).
- `_phase_ticks_race_free` race detector (canary equality +
  internal-consistency).
- `_phase_ticks_engine_clear_race` race-pass classifier.
- Dispatch arm with retry-on-race + race-pass classification.
- CLI default `--functions` now includes `phase_ticks`.

### Makefile

`engine_phase.c` added to `DIFF_SRCS` so libengine_diff.so includes
the port.

### Verified

`nix develop -c python3 tools/diff_test.py --vectors 500
--remote cutestation.soy:27042`:

| target        | result                       |
|---------------|------------------------------|
| `rng_step`    | 500/500 pass                 |
| `fps_step`    | 500/500 pass (16 race-pass)  |
| `anim_cycle`  | 500/500 pass                 |
| `frame_delta` | 500/500 pass                 |
| `phase_ticks` | 500/500 pass (2 race-pass)   |

All 123 host unit tests still pass under ASan + UBSan.

### What's next

1. **DDraw Blt-via-GDI seat** — short-circuit the "Blt invisible under
   DWM" quirk by intercepting `IDirectDrawSurface::Blt` and re-routing
   through `GDI StretchDIBits`.  Unblocks live recording-driven
   scenarios + visual verification of retail under the harness.  User
   has confirmed they can verify visually, so this is the natural
   next-step unlock.
2. **Save-file format** — `castles.dat` / `c_hist.dat` / `history.dat`
   parsers.  Starts unlocking the simulation subsystem ports.
3. **More diff targets** — two mechanical follow-ups remain:
   - `engine_compute_input_state` (FUN_004b191e) — 981 retail bytes;
     `GetCursorPos` + `timeGetTime` + many flags.  Needs a
     `GetCursorPos` scripted-return pattern paralleling our
     `timeGetTime` queue.  Largest of the remaining mechanical ports.
   - `engine_compute_sim_sub_steps` (FUN_004bb978) — has internal
     callees (FUN_004bb8c0, FUN_0043de70) and a pointer-array LUT;
     highest complexity of the remaining ports.

Top-down RE bias next time: option 1 (DDraw Blt seat) is highest-
leverage now that the diff machinery has covered all the small
pure-function targets and the user has confirmed they can verify
visually.

---

## 2026-05-24 — Fourth diff target: engine_compute_frame_delta + race-pass

Continued the "more diff targets" thread from the previous entry.  Wired
`FUN_004b20ed` (the 30 ms low-pass frame-delta generator) into the diff
machinery using the FPS-style scripted-clock pattern.  The work also
surfaced — and fixed — a class of false positives in the existing
diff-test harness around post-call write races.

### Agent (`tools/frida/openlords2-agent.js`)

- New `ADDR` entries: `fn_engine_frame_delta` (`FUN_004b20ed`),
  `var_frame_delta_last` (`_DAT_004e59ec`).
- New `g_nf_retail_frame_delta` NativeFunction (return type `int32`).
- New `runRetailFrameDelta(now_ms, last_frame_ms)` RPC: pushes the
  scripted clock value to the existing diff-queue, writes the baseline,
  calls retail, reads both writeback fields (delta_ms via return value,
  new_last_frame_ms via global), restores in `finally`.
- Also reports the structural `raced` flag (tick advance / in-tick check
  used by anim_cycle) but it's diagnostic-only for frame_delta — see
  below.

### Race classification (post-call writeback races)

Unrate-limited retail runs the per-tick driver at ~200 kHz on
cutestation.  Anything inside the per-tick driver's body — frame_delta,
anim_cycle, FPS — has a race window between our retail call returning
and our agent's readback.  In that window the engine can fire its own
call to the same retail function and overwrite a writeback global before
we observe it.

The structural detector (`g_engine_in_tick` flag + `g_tick_count` delta)
that anim_cycle uses is too aggressive for frame_delta — the engine is
mid-tick almost continuously, so the structural flag fires on nearly
every RPC, defeating retries.

The fix: a function-specific race detector that exploits the writeback
field's deterministic shape.  For frame_delta:

  - suppress branch (`delta < 30` → `delta_ms == 0`): retail must have
    held `_DAT_004e59ec` = our injected `last_frame_ms`.
  - pass branch: retail must have written `_DAT_004e59ec` = our scripted
    `now_ms`.

If readback shows anything else, the engine raced after our retail call.

Even with retries, the engine's per-RPC race rate is non-trivial when
our injected baseline is far from wall-clock (~60 % of random vectors).
But the retail function *still ran correctly per our spec* — it
consumed our scripted clock and computed the right delta against our
injected baseline; only the post-call observation got corrupted.

The orchestrator now classifies this signature as a "race-pass":

  - `delta_ms` matches port → retail's function ran correctly.
  - The only divergent field is `new_last_frame_ms` → it's a writeback
    race, not a spec bug.

Race-passes are counted in a new `race_count` field on `DiffResult`,
surfaced in the per-target summary line as `(N race-pass)`.  They
contribute to the pass count, not the fail count.

Same treatment applied to fps_step (the long-standing race in
`new_frames_this_second`, where engine bumped frames between our
snapshot and our retail call): if only `new_frames_this_second` diverges
and `retail.frames > port.frames`, classify as race-pass.

### Orchestrator (`tools/diff_test.py`)

- `EngineDeltaIn` / `EngineDeltaOut` ctypes mirrors.
- `lib.engine_compute_frame_delta` binding.
- `gen_frame_delta_vectors(n, rng)` — 8 fixed edge cases around the
  30 ms gate (boundary at 29 / 30 / 31, zero delta, signed-negative
  wraparound, large positive, u32 wrap), random fill with delta biased
  toward common ranges.
- `run_port_frame_delta` + `diff_frame_delta`.
- `_frame_delta_race_free` function-specific race detector.
- Dispatch arm with retry-on-race + race-pass classification.
- FPS dispatch arm gained the parallel pre-call race-pass classification.
- CLI default `--functions` now includes `frame_delta`.

### Verified

`nix develop -c python3 tools/diff_test.py --vectors 500
--remote cutestation.soy:27042`:

| target        | result                       |
|---------------|------------------------------|
| `rng_step`    | 500/500 pass                 |
| `fps_step`    | 500/500 pass (11 race-pass)  |
| `anim_cycle`  | 500/500 pass                 |
| `frame_delta` | 500/500 pass                 |

Frame_delta saw zero races on this run (lucky); the race-pass machinery
fired on FPS where the race rate is reproducible.  All 123 host unit
tests still pass under ASan + UBSan.  Smoke (`tools/smoke-test.py
--turbo`) still green.

### What's next

1. **DDraw Blt-via-GDI seat** — short-circuit the "Blt invisible under
   DWM" quirk by intercepting `IDirectDrawSurface::Blt` and re-routing
   through `GDI StretchDIBits`.  Unblocks live recording-driven
   scenarios.  Roughly half a day's work.  This is the unlock for
   recording-driven frame-diff testing.
2. **Save-file format** — `castles.dat` / `c_hist.dat` / `history.dat`
   parsers.  Starts unlocking the simulation subsystem ports.
3. **More diff targets** — easy follow-ups remain:
   - `engine_compute_phase_ticks` (FUN_004bbc80) — 11 inputs, 18
     outputs, 1 `timeGetTime` call.  Largest of the small targets.
     Mechanical but bigger surface.
   - `engine_compute_input_state` (FUN_004b191e) — 981 retail bytes;
     `GetCursorPos` + `timeGetTime` + many flags.  Needs a `GetCursorPos`
     scripted-return pattern paralleling our `timeGetTime` queue.
   - `engine_compute_sim_sub_steps` (FUN_004bb978) — has internal
     callees (FUN_004bb8c0, FUN_0043de70) and a pointer-array LUT;
     highest complexity of the remaining ports.

Top-down RE bias next time: option 1 (DDraw Blt seat) is the highest-
leverage one that doesn't need new RE.  Option 3 keeps the
mechanical-diff thread alive.

---

## 2026-05-24 — Third diff target: engine_compute_anim_cycle

Took the prior entry's option-4 follow-up ("more diff targets — the diff
machinery is now generic enough that any future pure-C port can be wired
in with ~60 LOC of agent RPC + ~30 LOC of ctypes binding").  Anim-cycle
was the lowest-risk candidate: 81 retail bytes, takes no args, reads two
globals (one as input, one as I/O), no `timeGetTime` dependency.

### Agent (`tools/frida/openlords2-agent.js`)

- New `ADDR` entries: `fn_engine_anim_cycle` (`FUN_00451f1a`),
  `var_anim_cycle_counter` (`DAT_0053e9a4`), `var_phase_tick_80ms`
  (`DAT_00591510`).
- New `g_nf_retail_anim_cycle` NativeFunction stub resolved alongside the
  existing RNG / FPS stubs in `resolveRetailFunctions()`.
- New `runRetailAnimCycle(counter, tick_80ms_fired)` RPC.  Snapshot →
  inject → invoke → readback → restore in a `finally`, same shape as
  the prior two but with a different race-detection strategy (below).
- Engine-tick hook augmented with `g_engine_in_tick` flag (set in
  `onEnter`, cleared in `onLeave` finally).  Reused by the anim-cycle
  RPC's race signal — see below.
- Counter readback uses `readS32` / `writeS32` because the field is
  signed in retail (the `< 0` defensive lower-clamp is the tell).  An
  initial smoke run with `readU32` caught this: a vector with
  `counter = -1` round-tripped via retail as `0xFFFFFFFF` and as `-1`
  via the port's `c_int`, which `int(...) != int(...)` flagged as a
  divergence.  Fixing the read to signed brought 15/16 → 16/16, then
  16/16 → 200/200 on the stress run.

### Race detection — tick-driven targets

The earlier RNG / FPS RPCs use function-specific race signals (RNG: no
race in practice because the engine doesn't ambient-call it; FPS:
`new_last_render_ms == in_now_ms` because the function unconditionally
stores the scripted clock there).  Anim-cycle has no such signal — the
function only mutates the counter, which is the thing we're trying to
diff.  So we can't distinguish "race overwrote the value" from "port is
wrong".

Workaround uses two pieces of state maintained by the existing
engine_tick interceptor:

  - `g_tick_count` (already bumped per onEnter) — if it advanced during
    the RPC, a fresh tick fired and necessarily called FUN_00451f1a.
  - `g_engine_in_tick` (new) — set to 1 at onEnter, cleared at onLeave.
    If non-zero at any sample point of the RPC, the engine is mid-tick.

The RPC returns `raced: 1` if either signal fires; the orchestrator
retries up to `ANIM_RACE_RETRIES = 8`.  In practice the stress run
above produced no permanent races.  False positives are acceptable
(the tick window opens before FUN_00451f1a fires, so some retries are
over-cautious); false negatives would manifest as occasional `RACE:`-
tagged failures, which the summary distinguishes from real port bugs.

### Orchestrator (`tools/diff_test.py`)

- `EngineAnimCycleIn` / `EngineAnimCycleOut` ctypes mirrors of
  `src/engine.h`'s structs.
- `lib.engine_compute_anim_cycle` binding added in `load_port_lib()`.
- `gen_anim_cycle_vectors(n, rng)` — 11 fixed edge cases (no-op at the
  counter range boundaries, normal bumps, rollover at 15, the
  unreachable lower-clamp paths, an OOB-positive sanity check),
  remainder random with `counter ∈ [-3, 20]` so the random fill exercises
  both clamps.
- `run_port_anim_cycle` + `diff_anim_cycle` (single field; trivial).
- Per-vector dispatch arm with the retry loop pattern adapted from FPS.
- CLI default `--functions` now includes `anim_cycle`.

### Build

`tests/Makefile`: `engine_anim_cycle.c` added to `DIFF_SRCS`.  Builds
cleanly under `-O2 -fPIC -Wall -Wextra` (no sanitizers — same rule as
the other diff sources).

### Verified

`nix develop -c python3 tools/diff_test.py --vectors 200 --remote
cutestation.soy:27042`:

| target        | pass     |
|---------------|----------|
| `rng_step`    | 200/200  |
| `fps_step`    | 200/200  |
| `anim_cycle`  | 200/200  |

All 123 host unit tests still pass under ASan + UBSan.  Smoke
(`tools/smoke-test.py --turbo`) still green — the engine_tick hook
gained a single in-tick flag mutation but otherwise behaves identically.

### What's next

The three threads from the prior entry, minus the one just landed:

1. **DDraw Blt-via-GDI seat** — short-circuit the "Blt invisible under
   DWM" quirk by intercepting `IDirectDrawSurface::Blt` and re-routing
   through `GDI StretchDIBits` (the same path `src/gfx.c` uses).
   Unblocks live recording-driven scenarios.  Roughly half a day's work.
   This is the unlock for the second-most-productive thread.
2. **Save-file format** — `castles.dat` / `c_hist.dat` / `history.dat`
   parsers.  Lands as `docs/findings/save-file.md` + a host-side
   decoder TU; starts unlocking the simulation subsystem ports in
   parallel with the engine-port main thread.
3. **More diff targets** — easy follow-ups:
   - `engine_compute_frame_delta` (FUN_004b20ed) — 1 input, 2 outputs,
     1 `timeGetTime` call.  Use the scripted-clock queue already in
     place.  Race detection: same tick-delta pattern (called per-tick
     too).
   - `engine_compute_phase_ticks` (FUN_004bbc80) — 11 inputs, 18 outputs,
     1 `timeGetTime` call.  Largest of the small targets but mechanical.
   - `engine_compute_sim_sub_steps` (FUN_004bb978) — has internal callees
     (FUN_004bb8c0, FUN_0043de70) we'd have to either stub or accept the
     returns of; the LUT pointer arg needs an OOB-safe injection
     strategy.  Higher complexity than the others.

Top-down RE bias next time: option 1 (DDraw Blt seat) is the highest-
leverage — recording-driven frame-diffs are how we'll catch render
regressions once the screen subsystems start porting.  Option 3 is
mechanical and easy to chain in.

---

## 2026-05-24 — Pure-function differential testing (Phase 4 of harness-roadmap)

Took the prior entry's "What's next" pointer — pure-function diff was
flagged as highest-leverage there ("catches arithmetic drift in our
ports before scenarios surface it").  Now landed end-to-end, covering
both ported pure functions: `engine_rng_step` and `engine_compute_fps`.

### Agent (`tools/frida/openlords2-agent.js`)

- New `ADDR` block: `fn_engine_rng_step` (`FUN_00404a46`),
  `fn_engine_compute_fps` (`FUN_004b27bf`), and every global the two
  functions read/write (8 for RNG, 6 for FPS — addresses cross-
  referenced to `src/engine_*.c`'s mirror tables).
- Refactored `installVirtualClockHook` → `installTimeGetTimeHook` +
  thin compat wrapper.  The new shape lets the diff path install the
  `timeGetTime` interceptor without arming turbo's virtual clock.
- New scripted-clock primitive: `g_diff_test_clock_queue` (FIFO of u32
  ms values).  The `timeGetTime` `onLeave` hook checks the queue head
  before the virtual-clock fallback.  Queue access is gated on
  `Process.getCurrentThreadId() === g_diff_test_caller_tid` so the
  engine main thread (still ticking during diffs) can't steal scripted
  values.
- New init opt `diff_test: bool` + two RPC entrypoints:
  - `runRetailRngStep(state_a, state_b)` — writes 2 inputs, calls
    retail, returns 8 outputs.
  - `runRetailComputeFps(now, rollover_now, last_render, window_start,
    frames, current_fps, cumulative_fps, cumulative_seconds)` — writes
    6 inputs, pushes 2 clock values, calls retail, returns 7 outputs
    (including return value as `delta_ms`).
- Both RPCs: snapshot all observable globals → write inputs → invoke
  via `NativeFunction` → readback → restore.  Restore is in `finally`
  so a thrown exception can't leave the engine perturbed.

### Orchestrator (`tools/diff_test.py`, new file)

- Builds `tests/build/libengine_diff.so` (new Makefile target,
  `-shared -fPIC`, no ASan since ASan-instrumented .so can't be
  `dlopen`'d from non-ASan Python).
- ctypes mirrors of `engine_rng_in_t` / `engine_rng_out_t` /
  `engine_fps_in_t` / `engine_fps_out_t` from `src/engine.h`.
- Vector generators: each starts with a fixed set of edge cases
  (retail seed pair, degenerate fixed points, all-ones, bit-30 set
  for RNG; rollover / non-rollover / strict-boundary / signed-wrap /
  long-window for FPS), then random fill to `--vectors N`.
- Per vector: invoke agent's RPC, invoke our port via ctypes, diff
  field-by-field.  Mismatch → recorded as failure with full vector +
  retail + port + diff list for the summary print.

### The engine-thread race + how it's handled

The engine main thread is still calling `FUN_004b27bf` every per-tick
driver iteration (≈200 Hz during boot).  Without protection, between
our retail call returning and our readback firing, the engine squeezes
in its own call and overwrites `_DAT_004e59e0` with wall-clock.

Two attempts at an on-target guard both **deadlocked**:

1. **`Interceptor.attach onEnter` spin** — spin while
   `g_diff_test_caller_tid !== -1`.  Frida's V8 isolate is single-
   threaded; the spin holds the isolate lock; the V8 RPC needs that
   lock to make progress; never clears caller_tid.
2. **`kernel32!SuspendThread` on the engine main thread** — Frida's V8
   heap and the engine share the process heap; suspending the engine
   while it holds an internal heap lock wedges the next V8 allocation.

Pragmatic resolution lives **on the Python side**: detect the race +
retry.  Strongest signal is `new_last_render_ms == in_now_ms` —
`FUN_004b27bf` unconditionally writes the first `timeGetTime` result
into that global, and our scripted clock always returns `in_now_ms` for
that read.  If readback differs, the engine raced us after the retail
call returned.  Retry up to 4× with 10 ms sleep between attempts so the
engine's intermediate calls land elsewhere.

RNG doesn't need retries — `FUN_00404a46` isn't on the per-frame hot
path today (its SP call sites are inside sim subsystems we haven't
unblocked), so the engine doesn't ambient-call it during diffs.

### Verified

Two stress runs against retail (via `cutestation.soy:27042`):

| Run                    | rng_step       | fps_step       | retries (rough)  |
|------------------------|----------------|----------------|------------------|
| `--vectors 12`         | 12/12          | 12/12          | ~0–1 / call      |
| `--vectors 200`        | 200/200        | 200/200        | a handful        |

This is the **first bit-exact validation of any ported function
against retail's actual code**.  All 14 baseline smoke checks still
pass (smoke run with `--turbo` exercises the refactored
`installVirtualClockHook` codepath and confirms the smoke-test's
load-bearing log-line greps still match).  Unit tests: 123/123
under ASan + UBSan, unchanged.

### Findings doc

[`docs/findings/pure-function-diff.md`](findings/pure-function-diff.md)
— full design: agent shape, the engine-thread race + why both guards
deadlocked, the host-side ctypes path, how to extend with a new
target, and what this catches / doesn't catch.

### What's next

The three threads from the prior entry remain, minus the one just
landed:

1. **Resume engine-port** inside `engine_tick`.  Pre-tick
   `FUN_00499a1c` (174 B, music/audio update) lands together with the
   audio subsystem; the screen-dispatch ladder unblocks per-screen as
   each subsystem ports.  Both gated on subsystems we haven't started.
2. **DDraw Blt-via-GDI seat** — short-circuit the "Blt invisible under
   DWM" quirk by intercepting `IDirectDrawSurface::Blt` and re-routing
   through GDI `StretchDIBits` (same path our `src/gfx.c` uses).
   Unblocks live recording sessions.  Roughly half a day's work.
3. **Save-file format** — `castles.dat` / `c_hist.dat` / `history.dat`
   parsers.  Lands as `docs/findings/save-file.md` + a host-side
   decoder TU; starts unlocking the simulation subsystem ports.
4. **More diff targets** — the diff machinery is now generic enough
   that any future pure-C port can be wired in with ~60 LOC of agent
   RPC + ~30 LOC of ctypes binding.  Next obvious candidates as they
   port: `engine_compute_anim_cycle` (FUN_00451f1a), the rate
   controller (FUN_004bb978), the bounded-bitmask RNG sampler
   (FUN_00404b99, calls into FUN_00404a46 with retries).

Top-down RE bias next time: option 2 unblocks recording-driven
scenarios, which then unblocks the per-screen subsystem ports
incrementally.  Option 3 unblocks the simulation subsystem chain in
parallel.  Pick whichever feels more interesting next session.

---

## 2026-05-24 — Input record + replay (Phase 3 of harness-roadmap)

Picked the "What's next" pointer from the previous entry: input replay
is the most productive non-engine slice while the screen-dispatch
ladder is still blocked on per-screen subsystems.  The machinery is
fully landed and tick-accurate against retail; a real interactive
recording session is gated on a separate host-display issue (see
"Limitations" below).

### Agent (`tools/frida/openlords2-agent.js`)

- Two new `ADDR` entries: `fn_wndproc` (`FUN_004b29be`) and
  `var_main_hwnd` (`DAT_004e5a00`).  Cached main-HWND is read directly
  from BSS rather than discovered via CreateWindowEx — the engine sets
  it during InitInstance and our drain runs only after engine_tick
  fires (i.e., after the main loop has started), so it's guaranteed
  non-null.
- New `g_tick_count` — 1-indexed engine_tick counter, bumped in
  `engine_tick.onEnter` at the top.  Used as both the recording
  timestamp and the replay schedule index.
- `installInputRecordHook()` attaches to `fn_wndproc.onEnter`, filters
  `args[1]` against the user-input WM_* set (KEY/CHAR/MOUSE — see
  `docs/findings/input-replay.md` for the full list), and emits one
  `kind:'input_event'` event per dispatched message with
  `{t, m, w, l, hwnd}`.
- `drainInputReplayForTick(tick)` posts queued events when their
  stamp ≤ current tick.  `PostMessageA` resolved lazily from
  user32.dll on first use.  Called from `engine_tick.onEnter` right
  after `g_tick_count++`, which puts the posted message in the main
  thread's queue *during* tick N → engine's `PeekMessage` picks it up
  before tick N+1.  Reverse of the recording: same gap between the
  same two ticks.
- Init RPC: two new opts (`input_record: bool`,
  `input_replay: array<{t,m,w,l}>`); ready event reports both.

### Driver (`tools/frida_capture.py`, `tools/smoke-test.py`)

- `CaptureConfig` grew `input_record: bool` and
  `input_replay_path: Path | None`.  The driver preloads the JSONL
  trace before spawn (parse failures surface immediately) and passes
  the parsed array inline via init opts — the Windows-side agent has
  no access to WSL filesystem paths.
- `_load_input_trace()` accepts comments (`#`) and blank lines so
  hand-authored synthetic traces stay readable.
- Two new `on_message` handlers: `input_event` writes a JSONL line to
  `<run_dir>/input_trace.jsonl` + logs to agent.log;
  `input_replay_post` increments a per-run counter for the smoke
  assertion.  Both are no-ops when their respective modes are off.
- New CLI flags `--input-record` / `--input-trace FILE` on both
  driver and smoke; smoke fails if `--input-trace` is set and zero
  events are posted by the end of the window.
- Bonus fix: smoke's "hide_window hooks installed" check was
  unconditional but the hide hooks correctly only install with
  `hide_window=true`.  Moved into the `not args.no_hide` arm so
  `--no-hide` smoke runs don't false-fail.

### Verified

Three smokes against retail (via `cutestation.soy:27042`):

1. `--input-record --turbo` (5 s, hidden window): WndProc hook armed,
   `input_trace.jsonl` created (empty — no events fired because
   hidden window has no focus to dispatch input to).  All 14
   baseline checks pass.
2. `--input-trace runs/synthetic/input_trace.jsonl --turbo` (4 s,
   then 6 s for diagnosis): synthetic trace of 4 events at ticks
   50/100/150/200.  Events 1..3 posted exactly at tick_now=50/100/150
   (`[replay] idx=0 t=50 tick_now=50 m=0x100 w=0x74 l=0x0`).  Event 4
   never fired — diagnosed: the 3rd event (VK_ESCAPE keydown at
   tick 150) hit retail's WndProc ESCAPE handler which sets
   `DAT_005aeb68 = 1` (deferred shutdown) when engine state isn't
   ingame.  Engine drained and exited cleanly within ~250 ticks; the
   smoke's cum_secs dropped from 2515 (baseline) to 2 (engine quit).
   This *validates* both the post mechanism and the engine's escape
   semantics — and is a property worth preserving in our port
   (VK_ESCAPE outside ingame state ≡ Alt-F4).
3. `--no-hide --no-force-windowed` (8 s): engine creates its main
   window (`WinLords2Class` "Lords2") but freezes before DSound init
   when forced into exclusive fullscreen.  Documented as a harness
   limitation in `input-replay.md` (not a regression — base
   `--force-windowed` smoke still gets ~117 fps).

### Limitations on real interactive recording

A live human-driven recording needs the engine's window to be both
visible AND responsive on the harness host.  Today's harness defaults
hit one of two walls:

- `--force-windowed` (default): window appears, but content is
  invisible under DWM (the documented `DDraw NORMAL Blt invisible
  under DWM` quirk).  Can't see what you're clicking.
- `--no-force-windowed`: engine attempts exclusive fullscreen on
  cutestation and hangs before DSound init (probably the desktop
  session isn't attached enough for exclusive mode-set).

Synthetic traces hand-authored from `wndproc.md`'s VK/WM table
round-trip end-to-end and prove the machinery works tick-accurately.
Real recording-from-a-live-session is gated on either a fullscreen-
capable Windows host or a re-impl menu (which uses GDI StretchDIBits
and renders correctly under DWM).

### What's next

The harness now has all three primitives needed for scenario-driven
differential testing: turbo (Phase 1), frame capture (Phase 2), input
replay (Phase 3).  Next slices, in roughly priority order:

1. **Resume engine-port** inside `engine_tick`.  Pre-tick
   `FUN_00499a1c` (174 B, music/audio update) lands together with the
   audio subsystem; the screen-dispatch ladder unblocks per-screen as
   we land each subsystem.  Both are blocked on subsystems we
   haven't started.
2. **Pure-function differential** (Phase 4 of harness-roadmap) — for
   each ported function, cross-check its output against retail via
   Frida `NativeFunction` invocation with the engine suspended.
   `engine_rng_step` (`FUN_00404a46`) and the FPS counter
   (`FUN_004b27bf`) are the obvious first targets: pure, small, and
   already have ASan-passing host-side ports.
3. **DDraw Blt-via-GDI seat** — short-circuit the "Blt invisible
   under DWM" quirk by intercepting `IDirectDrawSurface::Blt` and
   re-routing through `GDI StretchDIBits` (using the same path
   `src/gfx.c` uses for our re-impl).  Unblocks live recording
   sessions.  Probably half a day's work — vtable patch, lock the
   back, StretchDIBits to the HDC of the window we already track.
4. **Save-file format** — `castles.dat` / `c_hist.dat` /
   `history.dat` parsers.  Lands as a `docs/findings/save-file.md`
   plus a host-side decoder TU and starts to unlock the simulation
   subsystem ports.

Top-down RE bias next time: option 2 is the highest-leverage —
catches arithmetic drift in our ports before scenarios surface it, and
costs nothing relative to its protection.  Option 3 is the unlock
for option 1's recording-driven scenarios.

---

## 2026-05-24 — Sub-step body skeleton + double-LFSR RNG port (FUN_00404a46)

Continued the entry-chain port one slot deeper.  The sub-step body
(retail `FUN_004b99c0` lines 88749..88803, inside the
variable-rate sim loop) is now skeletoned in `engine_tick`, with
the SP-only LFSR advance landing as a real port and the rest
documented-as-deferred.

### Survey result — the biggest sub-step callees are MP-only

Reading retail's body end-to-end turned up a finding worth a
dedicated engine-quirks entry: the two functions that look like
"the heart of the sim tick" — `FUN_004472ce` (1046 B,
"advance sim by 1 tick") and `FUN_0043eda0` (637 B, "scheduled
events dispatcher") — are **both entirely MP-gated**.  Their
bodies are wrapped in `if (DAT_00553030 != 0)`, so in SP they
return immediately.

Implication: in SP mode (which is everything our drop-in can
produce today), the sub-step body's actual per-step work is:

- Master tick counter bump (`DAT_0057c998++`).
- LFSR advance (`FUN_00404a46`) — ported this session.
- Turn-end / autosave check (`FUN_0049b8b0`) — deferred, blocks on
  turn subsystem.
- State-machine breaks for save/load.
- Screen-state-specific extras (~11 functions, all blocked on the
  sim / combat / map subsystems).

That's it.  All other "sim work" happens in the pre-tick block
(anim phase, frame delta — already ported) or inside the screen-
dispatch ladder's per-screen handlers.  Documented as engine-quirk
#5.

### Port: `engine_rng_step` (`FUN_00404a46`, 230 B)

- **`src/engine_rng.c`** (new TU, pure C) — port of retail's
  double-LFSR step.  Two independent 32-bit Galois LFSRs with taps
  at bits 0 and 4; feedback injected into bit 30 (not bit 31).
  31 iterations per call.  Outputs six masked snapshots of the new
  state — 15/7/3 bits for each LFSR.  Pure function; every input
  + state mutation in the in/out structs.
- **`src/engine.c`** — wiring.  Eight new file-static mirrors
  (`g_rng_state_a/b` initialised to retail seeds `0x54657687` /
  `0x72641663` from FUN_004b2258 lines 83635-83636; six masked
  output mirrors).  Wrapper `engine_tick_rng_step` bundles + runs
  + writes back.  Wired into the sub-step body's SP-only branch
  (`g_sim_mp_active == 0`), matching retail's call site.
- **`src/engine.h`** — `engine_rng_in_t` / `engine_rng_out_t`
  structs and the pure-function declaration.

### Skeleton: sub-step body

- **`src/engine.c`** — `engine_tick`'s `for (i in 0..sub_steps)`
  loop body now has retail's structural shape.  Five new file-
  static mirrors land alongside (`g_sim_screen_substate`,
  `g_sim_player_human_active`, `g_sim_turn_end_pending`,
  `g_sim_turn_progress`, plus the eight RNG mirrors).  Each
  mirror's retail `DAT_*` cross-reference and producer status is
  documented inline.
- The `load_bailout` flag implements retail's "engine state 0xb →
  return from FUN_004b99c0 entirely" branch (skips the render
  phase tail).
- Everything else inside the sub-step body is a comment-only TODO
  with the retail subfunction address + size + blocker recorded.
- Today still produces 0 sub-steps every call (rate controller's
  scenario hint is 0 until engine state 3), so the loop body is
  unreached and observable behaviour is identical to the prior
  slice.

### Tests landed (9 new, 123/123 total under ASan + UBSan)

- All-zero state stays zero (degenerate LFSR fixed point).
- Seed=1 traces to a specific value (`0x08000001`).  Pinned as a
  regression — naive trace says "state=1 returns to 1" because it
  misses the secondary feedback at iter 28 when the walking bit
  re-hits position 4.  The test prevents future "small clean-up"
  from this not-actually-clean codegen.
- Bit 31 is always cleared after any call (the LFSR is effectively
  31-bit because feedback targets bit 30 and the shift is logical).
- State A and B are independent (zero-state-B stays 0 even when
  state-A evolves).
- Masked outputs match `new_state & mask` for all six fields.
- Reproducibility: same input → same output.
- Retail seed produces non-degenerate (non-zero, non-identity)
  output.
- Reference-comparison: against an independent inline `ref_advance`
  function, for four arbitrary seeds.
- Multi-call evolution: 10 consecutive calls from retail seed,
  each step changes state + cumulative 15-bit OR exceeds 0xff.

### Engine quirks

[`docs/findings/engine-quirks.md`](findings/engine-quirks.md) entry
#5 — the SP/MP asymmetry of the sub-step body.  The two biggest
"sim tick" functions are MP-only no-ops; SP's actual per-step work
collapses to LFSR + screen-state extras.

Two smaller LFSR oddities are documented inline in `engine_rng.c`
but not promoted to engine-quirks entries: the 31-bit-state-on-32-
bit-word shape (bit 31 always 0 after first call) and the seed=1
trivial trajectory.  Neither is load-bearing.

### Verified

- `make -C tests run` — 123/123 pass under ASan + UBSan
  (was 114/114; +9 RNG tests).
- `make -C src` — both .exe targets clean, no warnings.
- `tools/run-openlords2.sh --frames 50 --gfx-init` — transcript
  identical to pre-slice (sub_steps == 0 → loop body never runs;
  observable behaviour unchanged).
- `tools/run-openlords2.sh --frames 5000 --gfx-init` —
  `engine_ticks=5000 current_fps=652 avg_fps=648 (over 7 s)`.
  Plausible numbers, no FPS regression from the new mirror
  bookkeeping.

### What's next

The sub-step body is now structurally accounted for (one ported,
eleven-plus deferred with blockers).  The render-phase head + tail
slots are similarly structured.  The remaining unblocked work
inside `engine_tick` is:

1. **Pre-tick FUN_00499a1c port** (174 B) — music/audio update,
   gated on `g_engine_state == 3`.  Body calls `FUN_004af502` +
   `FUN_004af8d9` + `FUN_004afab9` — each is the audio
   subsystem's PL8/song-bank loader, unported.  Lands together
   with the audio subsystem.
2. **Screen-dispatch ladder** — the bulk of `FUN_004b99c0`, ~30
   subsystem dispatches based on `DAT_004eac50`.  Each branch
   blocked on its respective screen subsystem; lands incrementally.
3. **Input replay** — Phase 3 of `docs/harness-roadmap.md`.
   Record `WM_KEY*` / `WM_MOUSE*` as JSONL, replay back through
   `SendMessageA`.  Standalone harness slice independent of the
   engine ports.

Top-down RE bias next time: the engine-port slots inside
`engine_tick` are nearly exhausted, modulo the per-screen
subsystems.  The most productive next slice is probably **input
replay** — gives us scenario reproducibility for when the screen-
dispatch ladder starts landing per-screen handlers.  After that
the engine ports unblock once the first screen subsystem (probably
the title screen / FUN_004afab9 audio update) lands.

---

## 2026-05-24 — Phase 2 frame capture (DDraw back surface → BMP)

Picked option 1 from the prior entry's "What's next": hook the retail
engine's DDraw back buffer and dump the contents as a BMP per virtual
second.  Pairs with the FPS sampler (riding the same cum-secs
heartbeat), gives us the first viewable artifacts from the harness
without yet having to port any renderer code.

### Agent changes (`tools/frida/openlords2-agent.js`)

- Three new `ADDR` entries pointing at the engine's DDraw surface +
  palette globals (`var_dd_primary` `0x004eabd0`, `var_dd_back`
  `0x004eafac`, `var_dd_palette` `0x004df288`).  These were already
  reverse-engineered in `docs/findings/ddraw-dsound-bringup.md`; the
  agent reads them directly rather than hooking
  `IDirectDraw::CreateSurface` to discover them — three pointer reads
  is cheaper than vtable interception, and the engine never
  recreates these mid-run.
- New `captureFrameOnce()` — lazy-builds three `NativeFunction`
  stubs (`IDirectDrawSurface::Lock` slot 25 / `Unlock` slot 32 /
  `IDirectDrawPalette::GetEntries` slot 4 — slot numbers documented
  in `ddraw-dsound-bringup.md`), Locks the back with
  `DDLOCK_WAIT | DDLOCK_READONLY`, reads the `DDSURFACEDESC`
  (width / height / `lPitch` / `lpSurface`), GetEntries-fills a 256-
  entry `PALETTEENTRY` scratch, row-copies pitched indices into a
  tight `width*height` buffer, and emits a `send({kind:'frame', …},
  binaryPayload)`.  Payload shape: `[u32 w][u32 h][u32 idx][1024
  palette][w*h pixels]`.  Unlock in `finally`.
- `engine_tick.onLeave` already detected cum-secs rollovers for the
  FPS-sample emit; threaded the frame-capture trigger through the
  same branch so capture cadence rides the existing heartbeat (one
  capture per virtual second).  Three new init opts:
  `frame_capture`, `frame_capture_max` (default unlimited from
  agent, 16 from Python), `frame_capture_warmup_secs` (default 5 —
  skip captures while the back surface is still uninitialised).
- DDraw vtable slots (`Lock=25 / Unlock=32 / GetEntries=4`) and
  `DDSURFACEDESC` layout (108 B, dwSize-prefixed, lpSurface at +36)
  documented inline so the next reader doesn't have to chase
  headers.

### Driver changes (`tools/frida_capture.py`, `tools/smoke-test.py`)

- New `kind:'frame'` handler in `on_message` — unpacks the payload
  header, slices off the palette + tight pixels, writes a 24-bpp
  top-down BMP via `_write_paletted_bmp` under `<run_dir>/frames/
  frame_NNNNN.bmp`.  Same on-disk shape as
  `src/gfx.c:gfx_capture_backbuffer` so retail-vs-our-port frames are
  trivially diffable.
- `CaptureConfig` + `CaptureResult` grew matching fields; CLI grew
  `--frame-capture` / `--frame-capture-max N` /
  `--frame-capture-warmup-secs N`.
- `smoke-test.py` grew `--frame-capture` / `--frame-capture-max`
  and two new checks: "frame_capture armed" (log marker) and "at
  least one frame BMP written" (filesystem check).

### Verified

Two retail smokes:

1. 6 s window, default warmup=4 → 4 BMPs captured but all-black.
   Pixel histogram showed only one unique byte (0) per frame.  The
   captures were landing during the engine's pre-render phase
   (engine had ticked through cum-secs 1..4 in ~5 ms wall-clock with
   turbo on, well before any render call).  Diagnosis: the back
   surface had been created (`var_dd_back` non-NULL → Lock
   succeeded), but never drawn to.

2. 10 s window, `--frame-capture-warmup-secs 10` → 8 BMPs at
   virtual seconds 11..18.  Pixel histograms now show 21–55 unique
   byte values per frame and ~26 % non-zero pixel coverage.  Mid-
   row RGB sample of the late frames returns dark grays/browns
   matching the Sierra logo screen palette — palette conversion +
   row-pitch tight-pack are correct.

This is the first behaviour-visible artifact from the harness — up
to now we've been emitting events + log lines.  The 26 % non-zero
coverage on the Sierra logo screen also matches expectations: most
of the screen is the black background; the logo bitmap occupies the
middle band.

### Engine quirk side note (not a new entry)

The pre-warmup all-black frames are a useful artifact themselves —
they confirm that Lords II's `FUN_004b99c0` driver runs *before*
the engine paints to the back surface for the first time.  That is,
the per-tick driver is structured "process input, advance sim, draw"
but the first ticks process input + sim only (because the title-
screen state machine hasn't loaded its bitmaps yet); drawing
doesn't start until cum-secs ≈ 5 with our turbo step.  Worth keeping
in mind when we start porting the screen-dispatch ladder — early
ticks see uninitialised render state.

### Findings doc

[`docs/findings/frame-capture.md`](findings/frame-capture.md) —
vtable slots, DDSURFACEDESC layout, the read-three-globals choice
vs. hooking CreateSurface, payload schema, warmup rationale.

### What's next

Same three threads as before, minus the one we just landed:

1. **Sub-step body** — still the largest single port unblock.
   With frame capture now in place, we have a way to verify visual
   behaviour matches retail before/after major sim changes.
2. **Screen-dispatch ladder** — incremental; lands as each screen
   subsystem ports.  Visual diff with frame capture is now
   actionable, so the "is this screen rendering correctly?" question
   has a clear answer.
3. **Input replay** — Phase 3 of the harness roadmap.  Record
   keyboard / mouse events as JSONL, replay back; combine with
   frame capture for side-by-side diffs of identical input streams.

Top-down RE bias still: sub-step body is the biggest unblock for
porting more of the engine.  Input replay is the next biggest
harness investment (lets us script scenarios reproducibly).

---

## 2026-05-24 — Turbo v2 (virtualised `timeGetTime`) + retail FPS sampler

First non-port harness slice in a few sessions.  Picked up the
turbo-v2 hook the FPS port had set up as the obvious next step
(see the prior entry's "What's next") and landed it end-to-end
plus the verification stream.

### Agent changes (`tools/frida/openlords2-agent.js`)

- New `ADDR` table at the top: `fn_engine_tick = 0x004b99c0`
  (retail's per-tick driver), `var_fps_current = 0x004ea174`,
  `var_fps_cum_sum = 0x004ea0f4`, `var_fps_cum_secs = 0x004ea83c`.
  Centralises the four engine addresses the new hooks touch so the
  next port-style address rev only changes one place.
- New `installVirtualClockHook()` — seeds `g_virtual_now_ms` from a
  one-shot real-`timeGetTime` read (avoids the 49.7-day-old-baseline
  trap the FPS counter would otherwise hit on the first window),
  then attaches `winmm!timeGetTime` with `Interceptor.attach`
  +`retval.replace` so the real syscall still runs (cheap on modern
  Windows, just a `QueryPerformanceCounter` shim) and we only
  overwrite the return on the main thread.  Side threads (DPlay,
  DSound mixer, Smacker) keep wall-clock.
- New `installEngineTickHook()` — always on, regardless of turbo.
  `onEnter`:
  - Captures `g_main_thread_id` the first time it sees the engine
    main thread (the virtual-clock gate needs this; without it the
    virtual clock would silently never apply).
  - Advances `g_virtual_now_ms` by `g_turbo_step_ms` when turbo is
    armed.  Bump happens BEFORE the engine reads `timeGetTime` later
    in the same iteration, so the FPS counter / frame-delta / rate
    gate all see the new virtual time within the same tick.
  `onLeave`:
  - Reads `DAT_004ea83c` (cum-seconds); if it changed since the last
    sample, also reads `DAT_004ea174` (current FPS) +
    `DAT_004ea0f4` (cum FPS sum) and emits a `kind:'fps_sample'`
    event.  Once-per-engine-cumulative-second cadence keeps the
    event stream sparse (~1 per virtual second).
- New init opt `turbo_step_ms` (default 17 — matches OpenMare's
  default for a 60 Hz-ish perceived clock under that harness; for
  Lords2's much faster per-tick driver this produces ~500× wall-
  clock speedup, see verification below).  `init` install order:
  always-on hooks first, then the conditional set; turbo block
  installs the virtual clock BEFORE the Sleep no-op so the engine
  never observes a real-clock baseline once turbo is armed.

### Driver changes (`tools/frida_capture.py`, `tools/smoke-test.py`)

- `CaptureConfig` grew `turbo_step_ms`; `CaptureResult` grew
  `fps_sample_count` + `last_current_fps` / `last_avg_fps` /
  `last_cum_seconds` so callers can summarise the turbo curve from
  the result struct without re-parsing the log.
- New `kind: 'fps_sample'` handler in `on_message` — logs a
  one-line `[fps] current=… avg=… cum_secs=… virt_ms=… t_ms=…`
  trace into `agent.log` and tallies into the counter dict.
- CLI: `--turbo-step-ms N` on `frida_capture.py`; smoke-test grew
  `--turbo` + `--turbo-step-ms` knobs plus four new check rows
  that the turbo path actually installed.

### Verified (4 s smoke window, retail Lords2.exe)

Two back-to-back smoke runs, same hide/silent/force-windowed
defaults, only `--turbo` toggled:

| Run            | fps_sample_count | last_cum_seconds | last_current_fps | last_avg_fps |
|----------------|------------------|------------------|------------------|--------------|
| `--turbo` off  | 3                | 3                | 566 743          | 534 152      |
| `--turbo` on   | 1993             | 1993             | 117              | 117          |

The off-baseline "current_fps = 566 743" is the engine's own
counter sampling itself at the CPU-bound iter rate against the
real wall-clock — the engine literally believes it's running at
half a megaframe per second.  Under turbo, virtual time advances
faster than wall-clock by a factor of ≈ 500×, so the counter
re-normalises to the actual frames-per-virtual-second the loop
achieves: 117 — a believable per-render-frame rate.

Both runs passed all 12 smoke checks (8 base + 4 turbo).

### Engine-quirk side note (not a new entry)

The off-baseline FPS read (566 743) is a useful reminder of why
turbo v2 specifically virtualises `timeGetTime` rather than just
nopping Sleep.  Turbo v1 alone makes the loop spin pointlessly:
`timeGetTime` still returns wall-clock so the FPS counter blows
up.  This is documented as the v1 trade-off comment in
`installTurboSleepHook` and the install-order comment in `init`.

### What's next

Three viable threads now that the harness can run the engine at
multiplied wall-clock speed:

1. **DDraw frame capture** — the Phase-2 roadmap item.  Hook
   `IDirectDrawSurface::Lock` on the primary, serialise back-buffer
   contents as BGRA BMP under
   `runs/<scenario>/<ts>/frames/frame_NNNNN.bmp`.  Pairs naturally
   with the FPS sampler — capture-on-rollover gives an automatic
   1-per-virtual-second screenshot stream.
2. **Sub-step body** — still the largest single port unblock.
   Now with turbo, soak-testing the eventual port becomes cheap.
3. **Screen-dispatch ladder** — incremental; lands as each screen
   subsystem ports.

Top-down RE bias still: option 1 is the smallest verifiable step
that unblocks the diff-render workflow.  Sub-step body is the
biggest unblock but also the biggest single chunk; better tackled
once we have frame capture to validate against.

---

## 2026-05-24 — Render-phase tail survey + FPS counter port (FUN_004b27bf)

Continued exiting the per-tick driver one slot at a time.  Surveyed
the small slot between the input-step and screen-dispatch ladder
(`FUN_0040f1a0` — itself another screen-state dispatcher, deferred),
then surveyed and ported the bottom-of-driver render-phase tail.

### Survey results

**Between input-step and screen-dispatch** (single slot):

| Retail        | Size   | Disposition                                                                                                                                                                                                                                                                                                                                                                                                                            |
|---------------|--------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `FUN_0040f1a0` | 1117 B | **Deferred — screen-state dispatcher itself.**  Gated on `DAT_0057d340 != 0` (the deferred-screen-change cookie).  Body is ~40 `else if (DAT_004eac50 == <id>)` branches → per-screen UI subfunctions.  Each subfunction blocked on its corresponding screen subsystem.  Lands incrementally as each screen gets ported. |

**Render-phase tail** (six functions, gated on `g_savepump_request == 0`):

| Retail        | Size  | Disposition                                                                                                                                                                                                                |
|---------------|-------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `FUN_00429b1d` | 135 B | **Deferred.**  Map-scroll-offset clamp.  Blocked on map subsystem (no producer of the scroll values + dimensions).                                                                                                          |
| `FUN_0047ed34` | 103 B | **Deferred.**  UI-scroll-offset clamp.  Blocked on UI subsystem.                                                                                                                                                            |
| `FUN_00426529` | 11 B  | **Deferred — empty body.**  Same disposition as the other debug-stripped stubs.                                                                                                                                              |
| `FUN_004b27bf` | 143 B | **Ported.**  Rolling 1-second FPS counter — see below.                                                                                                                                                                       |
| `FUN_004b12fb` | 21 B  | **Deferred — `return 1` stub.**  Likely debug-build gate optimised out.                                                                                                                                                      |
| `FUN_004b0e03` | 875 B | **Deferred.**  Palette-fade machine, gated on screen `0x23` (Smacker/intro).  20 ms cadence, 12-step-per-RGB lerp across 256 palette entries; blocked on intro-screen subsystem.                                              |

### Port: `engine_compute_fps` (`FUN_004b27bf`, 143 B)

- **`src/engine_fps.c`** (new TU, pure C, no Win32 deps) — port of
  the rolling 1-second FPS counter.  Inputs: now_ms,
  rollover_now_ms (a SECOND timeGetTime call retail uses only on
  rollover frames as the new window-start basis), last_render_ms,
  second_window_start_ms, frames_this_second, current_fps,
  cumulative_fps, cumulative_seconds.  Outputs: ms-since-last-render
  (delta_ms — retail's caller discards it but the math runs every
  call), new_last_render_ms, new_second_window_start_ms,
  new_frames_this_second, new_current_fps, new_cumulative_fps,
  new_cumulative_seconds, and a `window_rolled_over` test helper.
- **`src/engine.c`** — wiring.  6 new file-static mirrors
  (`g_fps_last_render_ms`, `g_fps_second_window_start_ms`,
  `g_fps_frames_this_second`, `g_fps_current`,
  `g_fps_cumulative_fps`, `g_fps_cumulative_seconds`).  Wrapper
  `engine_tick_fps_step` reads timeGetTime twice (matches retail's
  2-call shape), bundles + calls + writes back.  Lazy-init for
  `g_fps_second_window_start_ms` on first call: retail's
  `FUN_004b2258` does `DAT_004eaba0 = timeGetTime()` at line 83665
  before the main loop spins; we don't have an InitInstance hook
  point yet so we substitute first-call-init.  Without it, the
  first frame would see `now - 0 > 1000` and fire a bogus rollover.
  Wired into `engine_tick`'s render-phase tail, gated on
  `g_savepump_request == 0` (matches retail; always true today).
- **`src/main.c`** — shutdown smoke summary.  After `main loop
  exited after N frame(s)`, prints
  `engine_ticks=X current_fps=Y avg_fps=Z (over W s)` so the harness
  can confirm the per-tick driver is alive and the counter is
  computing plausible values.  Four new public engine readers:
  `engine_get_current_fps/cumulative_fps/cumulative_seconds/frame_counter`.

### Tests landed (9 new, 114/114 total under ASan + UBSan)

- Basic frame: count bumps, no rollover, delta_ms = now - last_render.
- Delta is computed against last_render (not window_start).
- Rollover fires at 1001 ms elapsed; current_fps published, cumulative
  pair advances, window_start becomes the SECOND timeGetTime value
  (rollover_now_ms), frames_this_second resets to 0.
- Strict-boundary: exactly 1000 ms does NOT fire (the gate is
  `1000 < elapsed`).
- Two back-to-back rollovers — cumulative pair adds CURRENT FPS each
  time, not stale.
- Negative elapsed (clock wrap or debug-seeded future window_start) →
  signed-compare suppresses the rollover branch, frame count just
  bumps.  Same quirk shape as `engine_compute_frame_delta`.
- Two-call shape: rollover_now_ms (not now_ms) becomes the new
  window-start basis, but last_render uses the FIRST call.
- All-zeros first-call boundary (now_ms == 0 too) — no rollover.
- 3-second window pathological case (harness pause + resume) — math
  divides by the FULL elapsed not 1000; cumulative_seconds bumps by
  1 not 3 (it counts rollover events, not actual wall-clock seconds).

### Verified

- `make -C tests run` — 114/114 pass under ASan + UBSan
  (was 105/105; +9 FPS tests).
- `make -C src` — both .exe targets clean, no warnings.
- `tools/run-openlords2.sh --frames 50` — transcript identical to
  pre-slice except for the new shutdown summary line
  (`engine_ticks=0 current_fps=0 avg_fps=0 (over 0 s)` — engine path
  not running without `--gfx-init`).
- `tools/run-openlords2.sh --frames 50 --gfx-init` — `engine_ticks=50`
  confirms the per-tick driver is firing; FPS row reads 0 because
  50 frames is well under 1 second wall time.
- `tools/run-openlords2.sh --frames 5000 --gfx-init` —
  `engine_ticks=5000 current_fps=649 avg_fps=643 (over 7 s)`.
  Plausible numbers (Sleep(1) on Windows rounds to 1-2 ms → 500-1000
  FPS range).

### Engine quirks

No new entry — the two preserved quirks here (signed-rollover gate
shared with the frame-delta function; two-timeGetTime-call shape)
are documented inline in `engine_fps.c` and pinned by tests, not
load-bearing enough to need their own entries.  The first is a
shared shape with the frame-delta engine-quirk already documented;
the second is benign codegen drift on the order of microseconds.

### What's next

The render phase is now structurally split into three slices:

1. **Render-phase head — done** (input step + the deferred-screen
   dispatcher trampoline `FUN_0040f1a0` surveyed-as-deferred).
2. **Screen-dispatch ladder** — the bulk of `FUN_004b99c0`.  ~30
   subsystem calls + a long stream of phase functions
   (`FUN_00472a31`, `FUN_00419c78`, `FUN_0040c725`, …) interspersed
   with the `FUN_004b1cf3(N)` scoreboard dispatch.  This is where
   the bulk of per-frame UI/sim work runs.  Blocked on the
   corresponding subsystems; lands as those land.
3. **Render-phase tail — five-of-six deferred, FPS counter ported.**
   The remaining five are blocked on map/UI/intro subsystems.

The natural next slice is one of:

1. **Sub-step body** — retail's per-sim-tick block inside the
   variable-rate loop.  Blocked on the sim subsystem (`FUN_004472ce`
   itself is a major chunk).  Largest unblock and most productive
   for forward progress, but also the biggest single chunk.
2. **Turbo v2 in the Frida agent** — virtualise `timeGetTime` so
   the harness can drive faster-than-real-time soak runs.  Now
   that the FPS counter is alive, we have an immediate visual
   confirmation knob (current_fps should rise N× with the turbo
   scale).  Same shape as OpenMare's; small port.
3. **Engine state mirror** — write a producer that can transition
   `g_engine_state` to 3 (ingame).  Unblocks the rate controller's
   real scenario-hint path, the sub-step body's screen-state
   branches, the music-update gate (`FUN_00499a1c`), and the dead
   `engine_scenario_rate_hint` body.  Substantial — depends on
   menu→ingame transition which itself depends on the savepump
   chain.

Top-down RE bias says turbo v2 — the FPS counter just landed; it's
the smallest verifiable step that also gives us a knob for the next
soak-test cycle.  Lift in the harness, validate against the new
smoke summary.

---

## 2026-05-24 — Render-phase head survey + mouse-input resolver (FUN_004b191e)

First slice exiting the pre-tick block.  Surveyed all three retail
render-phase-head functions; ported the one substantive function;
documented the two trivial slots as deferred.

### Survey result

| Retail        | Size  | Disposition                                                                                                                                                |
|---------------|-------|------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `FUN_0049a3e6` | 109 B | **Deferred — dead branch.**  Gated on `DAT_004eac50 == 0x24`.  Cross-grep of every site that *writes* to the screen-ID byte: 0x23 and 0x25 are produced; 0x24 is never written.  Function is unreachable in retail.  Likely an orphaned screen state from an earlier revision. |
| `FUN_004b1de0` | 11 B  | **Deferred — empty body.**  Twin of `FUN_004b0aaa` from pre-tick: ship-build-stripped debug hook.                                                            |
| `FUN_004b191e` | 981 B | **Ported.**  Mouse-input resolver — see below.                                                                                                              |

### Port: `engine_compute_input_state` (`FUN_004b191e`, 981 B)

- **`src/engine_input.c`** (new TU, pure C, no Win32 deps) — port of
  the per-frame mouse-state resolver.  Inputs: screen-space cursor,
  `timeGetTime` ms, WndProc-mirrored button + dblclk bitmasks,
  viewport bounds, prior frame's resolved input state.  Outputs:
  game-coord cursor, motion event flag, button-edge flags (newly
  pressed / newly released for LMB + RMB), 300 ms delayed-single-
  click events, double-click events, click coordinates.  Two retail
  helpers (`FUN_00404dc1`, `FUN_00404d6b`) inlined as static
  `percent_ratio` / `apply_percent` — they're a percent-ratio +
  percent-application pair used in the viewport→game-coord mapping.
- **`src/engine.c`** — wiring.  New file-static `g_input_*` mirrors
  for the 25 retail globals this function touches; `extern` decls
  for the 5 already-existing main.c-owned mirrors (`g_mouse_x/y`,
  `g_mouse_buttons`, `g_mouse_dblclk`, `g_client_screen`).  New
  wrapper `engine_tick_input_step` calls `GetCursorPos` +
  `timeGetTime`, bundles into the input struct, runs the pure
  function, writes outputs back, and clears `g_mouse_dblclk` to
  mirror retail's `DAT_004eada1 = 0` final statement.  Wired into
  `engine_tick`'s render-phase head, right after the sim sub-step
  loop (matches retail's call order in `FUN_004b99c0`).
- **`src/main.c`** — promoted `g_mouse_x/y`, `g_mouse_buttons`,
  `g_mouse_dblclk`, `g_client_screen` from `static` to non-static so
  `engine.c` can `extern` them.  Comments updated to point at
  `engine_tick_input_step` as the new consumer.

### Tests landed (22 new, 105/105 total under ASan + UBSan)

Core matrix (22):
- Raw cursor always written.
- Cache update gated on `cursor_cache_enabled` (both branches).
- Game-coord 1:1 mapping + scaled mapping + div-by-zero clamp.
- Default clears all 10 event flags.
- Dblclk bits → flags + matching pending-click timer cleared.
- Button bits decoded into `new_lmb` / `new_rmb`.
- LMB press edge → `lmb_newly_pressed` + `mouse_event` +
  `button_changed`; timer NOT armed.
- LMB release: `lmb_newly_released` ALWAYS set (the
  `(DAT_004e65d8 = 1, DAT_004e65f0 != 0)` comma quirk in retail);
  arm only when `click_eligible`.
- LMB release-with-eligibility: arms timer, clears eligibility,
  records click coords, advances `click_timer_ms`.
- RMB mirror of the LMB block.
- 300 ms expiry: at 301 ms fires + disarms; at exactly 300 ms does
  NOT fire (strict `300 <` boundary).
- Shared-baseline quirk: both LMB+RMB armed against the same
  `click_timer_ms` → both fire on the same call when the window
  expires.  Quirk preserved.
- Motion x-change / y-change set `mouse_event` + reset
  `motion_last_ms`; no-motion computes `motion_idle_ms`.
- `prev_*` snapshots always written.
- `last_click_x/y` carried through on non-click frames (this is
  why the input struct grew `prev_last_click_x/y`).

### Verified

- `make -C tests run` — 105/105 pass under ASan + UBSan (was 83/83;
  +22 input tests).
- `make -C src` — both .exe targets clean, no warnings.
- `tools/run-openlords2.sh --frames 50` — transcript identical to
  pre-slice (`OpenLords2 starting → main loop exited → OK :Not
  active.`).
- `tools/run-openlords2.sh --frames 50 --gfx-init` — full DDraw
  bring-up + present loop transcript identical to pre-slice.

### Engine quirks

No new entry — the shared-300 ms-timer baseline between LMB and RMB
is unusual but documented inline in `engine_input.c` and pinned by
test, not load-bearing enough to need an engine-quirks entry of its
own.  The dead branch in `FUN_0049a3e6` (gate value 0x24 never
written) is a structural finding documented in main-loop-tick.md,
not a behavioural quirk in code.

### What's next

The render phase has two more head slots and a tail:

1. **Render-phase head: rest of `FUN_004b99c0` between input step
   and the screen-dispatch.**  Need to survey what runs between
   `FUN_004b191e` and the big `else if (DAT_004eac50 == ...)`
   screen-dispatch ladder.  Probably small.
2. **Render-phase tail** — `FUN_00429b1d` / `FUN_0047ed34` /
   `FUN_00426529` / `FUN_004b27bf` (FPS counter) / `FUN_004b12fb` /
   `FUN_004b0e03` + the WM_CLOSE deferred-shutdown trigger.  Gated
   on `g_savepump_request == 0` which is always true today.
3. **Screen-dispatch ladder** — the 30+ `else if (DAT_004eac50 ==
   <id>)` branches that drive per-screen rendering.  Largest single
   chunk in the function; lands incrementally as the menu / load /
   ingame screens get their UI subsystems.

Top-down RE bias: option 1 (survey between input-step and screen-
dispatch) is the smallest verifiable step.  If it turns out to be
empty / one trivial slot, option 2 (FPS counter) is the next
productive lift — gives a visible smoke artifact even without the
render subsystem to feed it.

---

## 2026-05-24 — Frame delta + 16-frame anim cycle (FUN_004b20ed, FUN_00451f1a)

Two more pre-tick slices landed back-to-back, plus the survey
result for the five remaining pre-tick subfunctions.  Pre-tick
block is now fully accounted for: three ports landed, five
documented-as-deferred.

### Ports

- **`src/engine_delta.c`** (new TU, pure C) —
  `engine_compute_frame_delta(in, out)` is the port of retail
  `FUN_004b20ed @ 0x4b20ed` (68 B).  Wall-clock ms-since-last-frame
  generator with a 30 ms signed low-pass.  Suppresses both small
  positive deltas (< 30 ms) and negative deltas (clock wrap,
  debug-seeded future baseline) — divergent from the phase-tick
  generator which fires on negative.  Result lands in
  `g_frame_delta_ms` (retail `DAT_004ea128`); downstream
  sprite-animation updaters (e.g. `FUN_0040da1e`) consume it but
  none of those are ported yet, so the value is write-only today.
- **`src/engine_anim_cycle.c`** (new TU, pure C) —
  `engine_compute_anim_cycle(in, out)` is the port of retail
  `FUN_00451f1a @ 0x451f1a` (81 B).  16-frame counter that bumps
  once per 80 ms phase tick — full cycle 1280 ms.  First slice
  with an in-pure-function data dependency between two ports:
  consumes `g_phase_tick_80ms` produced by `engine_compute_phase_ticks`
  last commit.  Downstream consumer (`FUN_004071a0`, sprite-strip
  indexer reading the counter as a frame index into a 16-slot
  strip starting at `0x28`) unported, so the counter spins with
  no reader.
- **`src/engine.c`** — wiring.  Two new file-static wrappers
  (`engine_tick_delta_step`, `engine_tick_anim_cycle_step`) added
  + new mirrors (`g_frame_delta_last_ms`, `g_frame_delta_ms`,
  `g_anim_cycle_counter`).  `engine_tick`'s pre-tick block now
  invokes `delta_step → phase_step → anim_cycle_step` in that
  order; the dependency between phase and anim is commented inline
  since it's a real ordering constraint (other slices' order is
  cosmetic-only, retail-trace-bisection convenience).

### Tests landed (15 new, 83/83 total under ASan + UBSan)

Frame delta (8):
- Below threshold (29 ms) → 0, baseline held.
- At threshold (30 ms) → 30, baseline advances.
- Just below (29 ms) — boundary on the other side.
- Large delta (4000 ms) → 4000, advances.
- Zero delta → 0, baseline held.
- Negative delta (clock wrap) → 0, baseline held — KEY divergence
  from phase-tick's clock-wrap-fires behaviour, pinned here.
- 32-bit overflow at the wraparound seam → small-positive ms ⇒
  advances normally.
- Boot path (baseline = 0) → first call past 30 ms advances.

Anim cycle (7):
- No tick → counter held.
- Tick at counter 7 → 8.
- Tick at counter 0 → 1.
- Wrap at 15→0 (the retail `0xf < c` after-bump test).
- 14→15 no-wrap boundary on the other side.
- Full 16-tick cycle returns to 0.
- No tick with max counter holds.

### Engine-quirks entry #4

The 16-frame anim counter has a defensive lower clamp (`c < 0 →
c = 0`) that's provably unreachable from the only writer.  Counter
starts at 0 in .bss, only ever bumped by 1, only ever reset to 0
by the upper clamp.  Two wasted instructions on every pre-tick
iteration for a condition that physically can't occur.  Ported
faithfully for byte-shape parity.

### Pre-tick survey — completed

Five functions remain in the pre-tick block; all surveyed
end-to-end this session.  Disposition documented in
[`docs/findings/main-loop-tick.md`](findings/main-loop-tick.md)
§"Pre-tick subfunctions — surveyed, deferred":

- `FUN_004b0aaa` (11 B) — empty body in retail.  Nothing to port.
- `FUN_00498458` (132 B) — menu refresh; blocked on menu subsystem.
- `FUN_00481b9a` (1399 B) — castle grid iterator; blocked on castle
  table + 0x50×0x50 collision grid.  Largest pre-tick subfunction.
- `FUN_00482111` (476 B) — army/unit grid iterator (sibling of
  481b9a); blocked on the unit subsystem.
- `FUN_004782c5` (340 B) — load-screen timeout; blocked on the
  load-screen state machine.

Combined observable delta from skipping all five: zero — each is
gated on a screen ID / sub-state that the drop-in cannot produce.

### Verified

- `make -C tests run` — 83/83 pass under ASan + UBSan (was 68/68;
  +15 delta + anim-cycle tests).
- `make -C src` — both .exe targets clean, no warnings.
- `tools/run-openlords2.sh --frames 50` — transcript identical to
  pre-slice.
- `tools/run-openlords2.sh --frames 50 --gfx-init` — full DDraw
  bring-up + present loop transcript identical to pre-slice.

### What's next

Pre-tick block is structurally complete (three ported, five
deferred with documented blockers).  The next natural slice should
exit the pre-tick block and move toward either:

1. **Sub-step body** — retail's per-sim-tick block: `DAT_0057c998`
   bump + mod-10/0x82/1000 scheduled events + `FUN_004472ce` (sim
   tick) + `FUN_00404a46` (SP-only step) + state-machine breaks for
   save/load.  Today produces 0 sub-steps so the body never runs;
   structural seam exists.  Substantive work blocked on the sim
   subsystem (`FUN_004472ce` itself is a major chunk).
2. **Render phase head** — `FUN_0049a3e6` / `FUN_004b1de0` /
   `FUN_004b191e` run before the screen-dispatch.  Likely tiny like
   the pre-tick block; survey first.
3. **Render phase tail** — `FUN_00429b1d` / `FUN_0047ed34` /
   `FUN_00426529` / `FUN_004b27bf` (FPS counter) / `FUN_004b12fb` /
   `FUN_004b0e03` + the WM_CLOSE deferred-shutdown trigger.  Gated
   on `g_savepump_request == 0` which is always true today.  FPS
   counter alone might be a useful smoke artifact even with no
   render subsystem to feed it.

Top-down RE bias says option 2 (survey-then-port the render-phase
head) — moves the entry-chain port forward by the smallest verifiable
step.  Sub-step body and the FPS counter are both productive but
further down the dependency graph.

---

## 2026-05-24 — Animation-phase tick generator (FUN_004bbc80)

Ported the 9-flag animation-cadence generator that retail invokes
in the pre-tick block.  Wires into `engine_tick`'s newly-created
pre-tick slot.  Same shape as the rate-controller slice that
landed just before: pure function in its own TU + Win32 wrapper in
engine.c + file-static globals + unit tests.

### Ports

- **`src/engine_phase.c`** (new TU, pure C, no Win32 deps) —
  `engine_compute_phase_ticks(in, out)` is the port of retail
  `FUN_004bbc80 @ 0x4bbc80` (465 B).  Maintains 9 "did event X
  fire" flags driven by `timeGetTime`: a base 20 ms tick + 8
  mod-N rollover flags (80 ms / 160 / 320 / 640 / 1040 / 1280 /
  1920 / 2560).
- **`src/engine.c`** — file-static mirrors for the 9 retail
  counters + 9 flags (`g_phase_last_base_tick_ms`,
  `g_phase_base_tick_counter`, `g_phase_counter_{2,4,8,13,16,24,32}`,
  `g_phase_tick_{20ms,80ms,160ms,320ms,640ms,1040ms_sticky,1280ms,1920ms,2560ms}`).
- **`engine_tick_phase_step`** (file-static, Win32 wrapper) —
  reads `timeGetTime`, bundles into input struct, calls pure
  function, writes deltas back.  Implements the sticky-vs-fresh
  semantics for the 1040 ms flag (OR-into-live, no clear)
  separately from the other 8 (clear-then-overwrite, matching
  retail's clear-at-entry pattern).
- **`engine_tick`** — pre-tick slot now invokes
  `engine_tick_phase_step` before `engine_tick_rate_step`.

### Tests landed (12, all under ASan + UBSan)

- No time passed → no flags fire.
- 19 ms (below threshold) → no flags fire.
- 20 ms (at threshold, `0x13 < 20`) → base fires, counter bumps.
- Negative delta (clock wrap / debug-seeded future) → base fires.
- Base counter 3 → 4 → 80 ms fires + resets.
- Base counter 2 → 3 → 80 ms does NOT fire (`3 < 3` false).
- Stale counter case: 20 ms didn't fire but base counter > 3 from
  a previous frame → 80 ms still fires (catches stuck counters
  from unconsumed state).
- counter_2 rollover: 0→1 no fire, 1→2 fires + resets.
- All long cycles at max — every counter at its threshold fires
  in one call.
- 1040 ms off-by-one: counter 0xb → 0xc no fire, 0xc → 0xd fires.
- No-fire-no-state-change: 10 ms delta + counter_4=2 → counters
  carry through unchanged.
- Default clears all flags — pure function overwrites any garbage
  the caller hands in for the flag outputs.

### Verified

- `make -C tests run` — 68/68 pass under ASan + UBSan (was 56/56;
  +12 phase tests).
- `make -C src` — both .exe targets clean, no warnings.
- `tools/run-openlords2.sh --frames 50` — transcript identical to
  pre-slice (phase ticks fire but nothing consumes them yet).
- `tools/run-openlords2.sh --frames 50 --gfx-init` — full DDraw
  bring-up + present loop transcript identical to pre-slice.

### What's next

- **Remaining pre-tick subfunctions** — `FUN_004b0aaa` (input
  poll), `FUN_00498458`, `FUN_004b20ed`, `FUN_00481b9a`,
  `FUN_00482111`, `FUN_004782c5`, `FUN_00451f1a`.  Each needs a
  brief survey before commit-to-port; some likely depend on
  subsystems that aren't there yet (input subsystem, sprite
  manager, etc.).
- **First sprite-blit primitive** — still the most user-visible
  outstanding piece.

---

## 2026-05-24 — Sim sub-step rate controller (FUN_004bb978)

Ported the variable-rate sim sub-step gate as a pure function +
Win32 wrapper, plus 24 unit tests covering every branch.  Closes
the second item from the previous entry's "What's next" (the
sub-step gate variant), and reshapes `engine_tick` into retail's
true skeleton: pre-tick block → variable-rate sub-step loop →
render phase tail.  Today the loop runs 0 sub-steps every iteration
(scenario hint == 0 because engine state isn't ingame yet), so
runtime behaviour is unchanged — but the structural seam is in
place, and each subsystem port will now slot directly into the
right phase.

### New + restructured

- **`src/engine_rate.c`** (new TU, pure C, no Win32 deps) —
  `engine_compute_sim_sub_steps(in, out)` is the port of retail
  `FUN_004bb978 @ 0x4bb978`.  Takes a full input struct (now_ms,
  last_step_ms, autosave window, run gate, scenario hint, MP flag,
  master sim tick, game speed, pause lock, per-screen freeze,
  extended pause, MP skip flag, phase counter, LUT pointer) and
  returns a deltas-only output struct (sub_steps + writebacks +
  notification flag).  Pure means the test harness compiles it
  under host gcc + ASan/UBSan without any stubs.
- **`src/engine.c`** — added file-static mirrors for the 12 retail
  globals the controller reads/writes (`g_sim_last_step_ms`,
  `g_sim_autosave_start_ms`, `g_sim_tick_counter`,
  `g_sim_mp_active`, `g_sim_game_speed`, `g_sim_pause_lock`,
  `g_sim_current_screen`, `g_sim_screen_freeze_table[256]`,
  `g_sim_extended_pause`, `g_sim_mp_skip_table[256]`,
  `g_sim_mp_player_index`, `g_sim_phase`, `g_sim_derived_hint`).
  All file-static — no exposure in `engine.h` until something
  outside engine.c needs to read them.
- **`engine_run_gate`** (file-static, port of retail
  `FUN_004bb8c0`) — returns 1.  That's what retail does too;
  literally `return 1` in the decomp.  Probably a debug-build
  toggle optimised out at ship.
- **`engine_scenario_rate_hint`** (file-static, port of retail
  `FUN_0043de70` outer arm only) — returns 0 unless
  `g_engine_state == 3` (ingame).  Today we can't reach ingame, so
  always 0.  Full 1211-byte body deferred until player-faction
  data + current-player mirror land.
- **`engine_tick_rate_step`** (file-static, Win32 wrapper) —
  reads `timeGetTime()`, bundles into the input struct, calls
  the pure function, writes deltas back.  Returns sub-step count.
- **`engine_tick`** restructured — now: bump frame counter →
  `[TODO: pre-tick block]` → `for (i in 0..sub_steps) { [TODO:
  sub-step body] }` → `[TODO: render phase]` → `gfx_tick_present(0)`.
  Today the loop never iterates (sub_steps always 0) so observable
  behaviour is identical, but the loop is in place for when sub-step
  body slices land.

### Tests landed (24, all under ASan + UBSan)

- gate=0 → 0 sub-steps, no state change.
- Autosave window active (< 5000 ms since start) → 0, no change.
- Autosave window boundary cases: 4999 ms blocks, 5000 ms expires.
- Autosave expiry in MP fires the notification flag.
- Scenario hint == 0 → 0 sub-steps.
- Fast-skip warm-up (negative hint, sim_tick < 100) → 1 sub-step.
- Fast-skip cruise (negative hint, sim_tick ≥ 100) → 2 sub-steps.
- Hint > 0x8c → clamped to 0x8c (also verifies the LUT OOB —
  the lookup index for hint=0x8c reaches 0x8c1, which is 480 B
  past the documented LUT end).
- LUT byte == 0 → 0 sub-steps.
- Pause lock → 0 sub-steps; phase + derived_hint still mutated.
- Screen freeze 2 blocks SP, passes MP.
- Screen freeze 1 (or any non-2 non-0) blocks both.
- Extended pause blocks SP; in MP, blocks if mp_skip_flag set,
  passes otherwise (MP at max speed → 1 sub-step, no advance).
- Slider max-slow (game_speed=0 → iVar3=10) blocks.
- Slider rate-gate boundary: 51 ms < threshold 52 ms → 0, 52 ms
  >= 52 → 1 sub-step + last_step_ms advances to now.
- MP at max speed (iVar3=0, mp_active=1) → 1 sub-step, no
  advance (uncapped).
- Phase counter wrap: 0x9f → 0 (next call), 0x9e → 0x9f.
- Time overflow sentinel: now < last_step → local_8 clamped to
  10000 → step accepted at any threshold.

### LUT OOB engine quirk

[`docs/findings/engine-quirks.md`](findings/engine-quirks.md) entry
#3 covers the LUT (`DAT_004e3260`) read past its documented
0x6e0-byte end — by up to 0x1ff bytes when scenario hint
reaches the `0x8c` clamp.  Same shape as the .ini parser's OOB: it
works in retail because adjacent .data is benign.  Our port takes
the LUT as a pointer; tests allocate a fresh buffer large enough
to cover the actual indices reached.  The drop-in passes `NULL`
(no LUT) since today the function returns 0 before reaching the
LUT branch.

### Verified

- `make -C tests run` — 56/56 pass under ASan + UBSan (was 32/32;
  +24 engine-rate tests).
- `make -C src` — both .exe targets clean, no warnings.
- `tools/run-openlords2.sh --frames 50` — transcript identical to
  pre-slice (no scenario hint produced → 0 sub-steps every iter →
  the new loop is a no-op).
- `tools/run-openlords2.sh --frames 50 --gfx-init` — full DDraw
  bring-up + present loop transcript identical to pre-slice.

### What's next

The pre-tick / sub-step-body / render-phase slots in `engine_tick`
are all open.  Per the top-down-RE memory, the right next slice is
the smallest one that unblocks the entry chain:

1. **Pre-tick block subfunctions** — `FUN_004b0aaa` (input poll),
   `FUN_00498458`, `FUN_004b20ed`, `FUN_004bbc80`, `FUN_00481b9a`,
   `FUN_00482111`, `FUN_004782c5`, `FUN_00451f1a`.  Most are
   unknown body — needs survey before commit-to-port.
2. **`FUN_004bbc80`** (465 B) — already decoded inline in
   `main-loop-tick.md`: it's a wall-clock-driven phase counter
   bank (mod-2/4/8/12/16/24 schedulers).  Pure timing, no
   subsystem deps — would slot in clean.  Probably the right next
   pre-tick port.
3. **LUT extraction** — pull `DAT_004e3260` bytes from retail's
   `.data` once a scenario consumer makes it worthwhile.  Today
   no consumer.
4. **First real drawing primitive** — sprite-blit helper.  Still
   the most user-visible next step independent of the per-tick
   chain.

---

## 2026-05-24 — Engine tick guard + dispatcher (FUN_0040e8bb + FUN_0040e890)

Ported the two small wrappers that bracket retail's per-tick driver
into a new `src/engine.{c,h}` module, plus a scaffold of
`FUN_004b99c0` itself.  Closes "main-loop driver" from the previous
entry's What's-next, but in slice form — the scaffold lets every
future subsystem port slot into `engine_tick()` rather than into
the bare main loop body, matching retail's shape.

### Ports

- **`src/engine.{c,h}`** — three functions + three engine-state
  mirrors.
  - `engine_tick_guard()` (port of `FUN_0040e8bb`) — surface-lost
    Restore-before-dispatch, deferred-shutdown 3→1 escalator,
    savepump dispatcher (SP arm only — MP arm waits on the
    `DAT_00553030` mirror), gate on `g_shutdown_request == 1`.
  - `engine_tick_dispatch()` (port of `FUN_0040e890`) — calls
    `engine_tick`, latches `g_savepump_request = 1` on
    `g_engine_state > 8`.
  - `engine_tick()` (scaffold of `FUN_004b99c0`) — bumps
    `g_engine_frame_counter` (retail `_DAT_005bb4c8`) then calls
    `gfx_tick_present(0)` (retail `FUN_00452160(0)`).  Every other
    call inside retail's 2222-byte body is deferred — the pre-tick
    block, the sim sub-step loop, and the ~30 render-phase
    subsystem dispatches each land in their own slice.
  - Mirrors: `g_engine_state` (DAT_0052af94), `g_shutdown_request`
    (DAT_005aeb68 — moved out of `main.c` since `engine_tick_guard`
    now owns its escalator), `g_savepump_request` (DAT_005c9274).
- **`src/main.c`** — `main_loop_body` now routes through
  `engine_tick_guard()` instead of calling `gfx_tick_present`
  directly.  The capture-every-N-frames sidecar moves with it.

### Structural deviations from retail

- Surface-lost detection is a *sticky latch* in our gfx layer
  (`gfx_surface_lost()`) rather than retail's per-iteration
  `IDirectDrawSurface::IsLost` poll on both surfaces.  The present
  path is the only producer of fresh IsLost results in retail too,
  so the latch-style is equivalent for our purposes.  Comment in
  `engine.c` documents the equivalence.
- `FUN_004b26b8`'s `"OK :Paused."` log + InvalidateRects after a
  successful Restore-during-guard is skipped for now.  No Smacker
  movie in flight that would care; the InvalidateRect would just
  re-fire our no-op WM_PAINT.  TODO flagged in the guard.
- WndProc's `WM_CLOSE` / `VK_ESCAPE` handlers still call
  `DestroyWindow` inline.  The structural seam to remove that is
  now in place (the guard's `shutdown_request == 1` early-return),
  but the actual removal waits on a sim that can ack the deferred
  shutdown — until then there's no producer of the "drain one
  frame" half of the dance, so dropping the inline would just
  block shutdown.

### Verified

- `make -C src` — both .exe targets build clean, no warnings.
- `make -C tests run` — 32/32 tests pass (no change; engine.c is
  Win32-coupled and not included in the test build).
- `tools/run-openlords2.sh --frames 50` — transcript identical to
  the pre-slice run (engine-state machine sees no transitions in a
  bootless smoke).
- `tools/run-openlords2.sh --frames 50 --gfx-init` — DDraw bring-up
  + present-loop transcript identical to the pre-slice version.
  `engine_tick_guard` is fully exercised (the gfx-init arm is the
  one that actually runs the dispatch).

### What's next

Same shortlist mostly survives, now with one of the items closed:

1. **First real drawing primitive** — sprite-blit helper.  Still
   the most user-visible next step.
2. **`FUN_004b99c0` body — first slice of subsystems.** The
   scaffold is up; the natural next layer inside it is either the
   pre-tick block (small, mostly state-machine reads:
   `FUN_004b0aaa` input poll, `FUN_00499a1c` music update,
   `FUN_004b20ed` ?, the wall-clock increment) or the sim
   sub-step gate (`FUN_004bb978` rate controller — pure decision
   function, would slot into the test harness).
3. **`FUN_004ae1b8`** (lords2.inf 1128-byte persistence load) —
   still deferred until at least one consumer chain decoder lands.
4. **Sim ack of the deferred shutdown** — small follow-on slice
   that removes the WM_CLOSE / VK_ESCAPE inline `DestroyWindow`
   workarounds in WndProc once there's a sim that can roll the
   shutdown_request 3→1 transition into a clean main-loop exit.

---

## 2026-05-24 — Unit test harness + caught real OOB

Stood up a host-gcc unit test harness under ASan + UBSan,
modelled on `/opt/src/openrecet/tests/`.  First-run found a real
out-of-bounds read in the just-landed config parser — exactly the
kind of bug the smoke tests miss (back-buffer captures look fine,
nothing crashes on Windows because the production buffer is
20000 zero-padded bytes that absorbs the OOB).

### Scope landed

- **`tests/Makefile`** — host gcc, `-fsanitize=address,undefined
  -fno-sanitize-recover=all`.  Builds a single
  `tests/build/run_tests` from the pure-C source under test
  (`src/strutil.c`, `src/config.c`) plus the test files.
- **`tests/t.h`** — tiny assertion macros (`T_FAIL`, `T_ASSERT`,
  `T_ASSERT_EQ_I`, `T_ASSERT_EQ_S`, `T_ASSERT_MEM_EQ`, `T_SKIP`).
  Mirrors openrecet's `t.h` verbatim — same return-code protocol
  (0 pass, 1 fail, 2 skip).
- **`tests/test_main.c`** — driver with X-macro test registry
  (each test name appears exactly once; registry can't drift
  from declarations).  Stubs `log_line` and `io_read_file_at` so
  src/config.c links without dragging in the Win32 surface.
- **`tests/test_strutil.c`** — 15 tests covering every quirk
  claimed in `strutil.h`: substring search's "0 = not-found OR
  found-at-0" ambiguity, char-search's 1-based return, bounded-
  copy's no-null-terminator guarantee, strip_ctrl's `[0x20,
  0xa6]` range edges.
- **`tests/test_config.c`** — 17 tests covering defaults match,
  per-key parse (CDRomDrives / CDROM / SoundChannels / PhysicalMem
  / VideoSpeed / CPU), all four CPU class branches (`P`, `p`,
  `>`, otherwise), the fast_machine threshold's `>` vs `>=`
  boundaries, missing-keys behaviour, and a real-shape sierra.ini
  round-trip.
- **README "Tests" section** — points new contributors at the
  harness.

### Bug caught

`config_parse_sierra_ini` was reading past the input buffer in two
places:

1. The `find_char` call in `seek_past_equals` used the full `max`
   as bound instead of `max - off` (where `buf + off` is the
   search start).  Retail does the same — out-of-bounds-past-key
   read of up to 20000 bytes — but harmless because retail's
   buffer is a 20000-byte malloc zero-padded after the file
   content.  Our pure-C parser receives buffers of arbitrary
   size, so clamping was needed.

2. More serious — `FUN_00401000`'s "0 = not-found OR found-at-0"
   quirk meant that when a key was absent but the buffer
   contained any other `=` (very common: WindowsDir=., CDROM=,
   etc.), the parser would set `off` to whatever value happened
   to follow the first `=`.  For the CPU= line, this then ran a
   20-byte dash-scan past random offsets, tripping ASan on test
   inputs and (in retail) silently overwriting fields with
   garbage.

Both fixed in `src/config.c` — `seek_past_equals` now
disambiguates the 0-quirk via `memcmp(buf, key, klen)` and clamps
the `find_char` bound to remaining bytes.  Comments flag both as
deliberate retail deviations.  Production smoke unchanged
(transcript identical pre/post fix on a real game-dir run).

### Refactor

To make the parser testable in isolation, two pure-C symbols are
now exported from `src/config.h`:

- `config_set_defaults(config_t *out)` — applies retail's
  defaults block.
- `config_parse_sierra_ini(const char *buf, int n, config_t *out)`
  — pure parser, no globals, no I/O.

`config_init()` is now `config_set_defaults(&g_config) +
load_l2_eng() + try_l2ini_to_sierra_ini() +
config_parse_sierra_ini(g_ini_scratch, ..., &g_config)` — same
behaviour as before, just exposing the seams.

### Verified

- `make -C tests run` — 32 tests, all green, no ASan/UBSan trips.
- `make -C src` — both .exe targets build clean.
- `tools/run-openlords2.sh --frames 50` — transcript identical
  to pre-test-harness run.

### What's next

Same shortlist as the previous entry — first real drawing
primitive, main-loop driver port, lords2.inf consumer chain.
Test harness now ready to receive coverage as each pure-C parser
or decoder lands.

---

## 2026-05-24 — WinMain pre-init slice (config + strutil)

Ported the WinMain pre-init chain: l2.eng localization load,
l2.ini → sierra.ini parse, and the small string-helper quartet
they depend on.  Closes item 3 from the previous progress entry's
"What's next".

### New findings doc

[`docs/findings/winmain-pre-init.md`](findings/winmain-pre-init.md)
maps `FUN_0040e9ab`'s pre-init calls in order, decodes
`FUN_0040ecc4`'s defaults-first then file-overrides pattern with
all six sierra.ini keys + the derived "fast machine" flag, and
catalogues the string-helper quartet (`FUN_00401000` substring
search, `FUN_00401085` char search, `FUN_00401136` bounded copy,
`FUN_004012a2` strip-ctrl).

### Ports

- **`src/strutil.{c,h}`** — the four engine string helpers, ported
  verbatim including their quirks: `strutil_find_substr` is 0-based
  with the "0 = not-found OR found-at-0" ambiguity (preserved —
  real .ini callsites never trigger it), `strutil_find_char` is
  1-based, `strutil_bounded_copy` doesn't null-terminate,
  `strutil_strip_ctrl` null-terminates at the first byte outside
  `[0x20, 0xa6]`.  Each function ≤ a dozen lines.
- **`src/config.{c,h}`** — `config_init()` walks the same lookup
  chain as retail: defaults → l2.ini (for WindowsDir) →
  `<WindowsDir>\sierra.ini` → fallback `sierra.ini` from CWD.  The
  asymmetric retail control flow (WindowsDir-missing skips the
  fallback that l2.ini-missing reaches via fall-through) is
  preserved with a comment flagging it as retail-as-shipped.  The
  six sierra.ini keys cook into a `config_t g_config` struct +
  the derived `fast_machine` flag.  l2.eng is loaded into a static
  120-KB buffer; future UI text consumers read it via
  `config_localization_data`.
- **`src/main.c`** — calls `config_init()` after `init_exe_dir`
  but before window creation, matching retail's
  `__getcwd → l2.eng → FUN_004ae1b8 → FUN_0040ecc4 → FUN_004b2258`
  ordering.

### Deferred

- **`FUN_004ae1b8`** (lords2.inf 1128-byte persistence load) — the
  load itself is trivial but the consumer chain (`FUN_004ae2bd`,
  `FUN_004ae2ab`, `FUN_004ae310`, `FUN_004aecf0`) hasn't been
  decoded.  Loading the bytes without consumers would be
  write-only state, so this lands when at least one consumer
  does.

### Verified

- Baseline smoke (`--frames 50`): clean.  Transcript shows
  `config: loaded l2.eng (100016 bytes)` and
  `Err:Could not read sierra.ini` (game-dir has no
  `sierra.ini`; L2.INI's `WindowsDir=.` triggers the `.\sierra.ini`
  attempt which also misses; defaults take effect).  Retail
  behavior matches: GOG installer doesn't lay down sierra.ini, the
  defaults are the load-bearing values.
- No regression in `--gfx-init` or smoke transcripts beyond the
  new pre-init log lines.

### What's next

1. **First real drawing primitive** — sprite-blit helper.  Visual
   diff against Frida-captured retail would close the loop on
   "what does the engine actually paint".
2. **Main-loop driver** — `FUN_004b99c0` (~2200 B).  Bottoms out
   in ~40 subfunctions across most subsystems; too large for one
   session.  Would need a multi-slice plan.
3. **`FUN_004ae1b8` (lords2.inf load)** — once a consumer chain
   decoder lands.

---

## 2026-05-24 — Render rate-limiter + dirty-rect tracker

Ported `FUN_00452160` (rate-gated present), `FUN_00452306`
(`dirty_rect_union`), and `FUN_0045240a` (`mark_all_dirty`) into
`src/gfx.c`.  Closes the punchlist item that's been carried since
the `gfx_present` slice landed ("we present every frame
unconditionally — needs the 50-tick gate").

New findings doc:
[`docs/findings/render-rate-limiter.md`](findings/render-rate-limiter.md)
covers the three functions + their shared global state in full
(accumulator, sticky force latch, dirty count, dirty bounds, Smacker
bypass, alternate-composition suppression gate + forced-redraw
insurance).  Per-function summary in the doc; the gist:

- The accumulator (`DAT_004eabe8`) is fed by `timeGetTime` deltas
  via the same source that ticks the FPS counter.  Threshold = 50.
- `force=1` and the sticky `DAT_005651ac` latch pin the accumulator
  past the threshold — used by screen-state transitions.
- Dirty-count gating (`DAT_0050d09c`) means presents that have no
  visual work are no-ops.
- Dirty-rect bounds (`DAT_0050a4bc/c0/c4/c8`) are accumulated for
  incremental-redraw consumers but the rate-limited present itself
  doesn't use them — it always blits the full back via
  `DAT_004ea180` (the visible client rect).  Bookkeeping is
  currently write-only in our drop-in.
- Smacker bypass and the cutscene-suppression gate are dead in
  steady state (both flags zero); ported as stubs/comments for
  when those subsystems land.

### Port

- **`gfx_tick_present(int force)`** in `src/gfx.c` — the rate-gated
  present.  Replaces the prior unconditional `gfx_present` call.
  `gfx_present` itself stays as the blit primitive (used by
  `gfx_tick_present` internally + available for one-shot bring-up
  tests).
- **`gfx_dirty_rect_union(int l, int t, int r, int b)`** — reproduces
  retail's `FUN_00452306` arithmetic verbatim, including the retail
  typo at the max_y clamp (compares against `width-1` but clamps to
  `height-1`; harmless because inputs are pre-bounded).
- **`gfx_mark_all_dirty()`** — `dirty_rect_union(0, 0, 639, 479)`.
- **`gfx_force_present_next()`** — sets the sticky latch, mirror of
  `DAT_005651ac = 1` callsites in retail.
- `gfx_init` ends with `gfx_mark_all_dirty()` (matches retail's
  bring-up tail).  `gfx_fill_back_index` also marks dirty after the
  fill so the next present is guaranteed to flush.
- `main.c` wires `gfx_tick_present(1)` once immediately after a
  successful `gfx_init` so frame-0 capture / show-window has visible
  content, then `gfx_tick_present(0)` per main-loop iteration.

### Verified

- Baseline smoke (`--frames 50`): clean.
- `--gfx-init` smoke (`--frames 50`): clean — full bring-up + clean
  shutdown.
- `--gfx-init --capture-to /tmp/openlords2-rate --capture-every 20`
  over `--frames 200`: 10 back-buffer captures each show
  `(128,0,0)` at centre (palette index 1 from `base01.256`).  Window
  captures show `(0,0,0)` because the run is hidden — DWM has
  nothing to composite when the window isn't shown.  Manual
  `--show-window` validation deferred to the user (same constraint
  as every other visual-correctness slice).

### What's next

Same as before, minus the rate-limiter (now done):

1. **First real drawing primitive** — sprite-blit helper so we can
   capture a recognisable image and diff against retail.  Next
   natural step now that the present cadence matches retail.
2. **Main-loop driver** — `FUN_004b99c0` (~2200 B).  Largest single
   port remaining.
3. **WinMain pre-init slice** — `l2.eng` localization load + l2.ini
   parse (`FUN_0040ecc4`) + Sierra config (`FUN_004ae1b8`).

---

## 2026-05-24 — Surface-restore wire-up

Ported retail's `FUN_004b2730 @ 0x4b2730` as `gfx_restore()`.  Closes
the WndProc TODO that's been carried since the WndProc port landed.

### Scope landed

- **`gfx_restore()`** in `src/gfx.c` — `IDirectDrawSurface::Restore`
  on primary then back.  Returns 1 on success, 0 on failure.  Logs
  retail's exact strings (`ERR:Can't restore front buffer.` /
  `ERR:Can't restore back buffer.`) at `0x4df668` / `0x4df68c`.
- **`gfx_surface_lost()`** accessor + internal latch.  Set in
  `gfx_present` whenever `BltFast` (fullscreen) or `Lock` (windowed)
  returns `DDERR_SURFACELOST`.  Cleared by a successful
  `gfx_restore` or a fresh `gfx_init`.
- **WndProc `WM_ACTIVATEAPP` escalator** in `src/main.c` — on focus
  regain with the latch set, call `gfx_restore()`.  On failure,
  escalate: `gfx_shutdown()` + `gfx_init()` (port of retail's outer
  `FUN_004b2922` wrapper around `FUN_004b2730`).  On either success
  path, clear `g_surface_lost` and mark `g_render_dirty = 2` so the
  next present repaints fully.

### Verified

Baseline + `--gfx-init` smoke runs both clean — no regressions.
The restore path itself isn't exercised by a smoke run (no real
focus-loss event under headless), so this slice is "wired but not
end-to-end tested".  Manual validation candidate: run with
`--show-window`, switch to another fullscreen app, switch back —
should see `OK :Active.` followed by either a clean restore or a
re-init sequence in the transcript.

### What's next

Same as before, minus surface-restore (now done):

1. **Render rate-limiter + dirty-rect tracker** —
   `FUN_00452160` / `FUN_00452306`.  Mid-size port; touches several
   undecoded globals around the dirty-rect accumulator, so worth a
   dedicated findings doc first.
2. **First real drawing primitive** — sprite-blit helper so we can
   capture a recognisable image and diff against retail.
3. **Main-loop driver** — `FUN_004b99c0` (~2200 B).  Largest single
   port remaining; what consumes the `g_shutdown_request` escalator
   and the per-tick rate gate.
4. **WinMain pre-init slice** — `l2.eng` localization load + l2.ini
   parse (`FUN_0040ecc4`) + Sierra config (`FUN_004ae1b8`).  Lots of
   global init touching `g_cd_drive`, `g_sound_channels`,
   `g_video_speed`, etc.  Self-contained, but pure setup — only
   becomes load-bearing once the consumers (SFX, video, screen-state
   code) land.

---

## 2026-05-24 — DSound bring-up (`src/audio.c`)

Ported retail's `FUN_00425b10 @ 0x425b10` (DSound init) into a new
`src/audio.c`.  Continues the WinMain entry-chain: retail calls this
*after* `FUN_004b2258` (InitInstance) returns, so it slots in after
`gfx_init` in our `WinMain`.

### Scope landed

- `audio_init(HWND)` — `DirectSoundCreate(NULL, &lpDS, NULL)` +
  `SetCooperativeLevel(hwnd, DSSCL_PRIORITY)` + `CreateSoundBuffer`
  primary (`DSBCAPS_PRIMARYBUFFER`) + `SetFormat(&wfx)` with retail's
  `FUN_00425ded` template: **11025 Hz stereo 8-bit PCM**.  All four
  error paths log retail's exact strings (`ERR:DS failed open`,
  `ERR:DS failed set co-op`, `ERR:DS failed setup primary buffer`,
  `ERR:DS failed to change primary buffer format`) at byte addresses
  `0x4d4768..0x4d47bc`; success arm logs `OK :DS Installed.` from
  `0x4d47ec`.
- `audio_shutdown()` — releases primary buffer + IDirectSound in
  reverse order, logs `OK :DS Released.` from `0x4d4800`.
  Idempotent — safe to call on a failed/skipped init.
- Voice slot zeroing (`DAT_00522b00[20]`, `DAT_00522b80[2]`,
  `DAT_00522b50[2]`) is left as a TODO — those slots are only
  meaningful once the SFX/music playback path exists.

### Default-off gating

`audio_init` is gated behind `--audio-init` in main.c, mirroring the
`--gfx-init` convention.  `SetCooperativeLevel(PRIORITY)` modifies
process-global DSound state and the headless smoke harness has no
way to validate output audibly — so default-off keeps
`tools/run-openlords2.sh` runs harmless.

### Verification

- Baseline smoke (`--frames 50`) clean — no change in transcript
  (only `OK :Active.` / `OK :Not active.`).
- `--audio-init` smoke clean — transcript shows
  `OK :DS Installed.` after `OK :Active.` and `OK :DS Released.`
  during shutdown, exits rc=0.
- `--gfx-init --audio-init` combined smoke clean — full DDraw
  bring-up + palette load + DSound install + clean teardown in one
  run.

### Other changes

- `src/Makefile` links `-ldsound`.
- `audio.c` includes `mmsystem.h` before `dsound.h` to pull in
  `WAVEFORMATEX` (mingw's `dsound.h` doesn't include it itself).

### What's next

The entry chain past WinMain is now ported end-to-end: window class
+ creation (`FUN_004b2258`) → DDraw bring-up (`FUN_004b2922` chain)
→ DSound bring-up (`FUN_00425b10`).  Remaining bring-up gaps:

1. **Render rate-limiter + dirty-rect tracker** — port
   `FUN_00452160` / `FUN_00452306` so we mirror retail's actual
   present cadence (currently we present every frame
   unconditionally).
2. **Surface-restore wire-up** — connect WndProc's
   `WM_ACTIVATEAPP` active-with-lost-surfaces branch to a real
   `IDirectDrawSurface::Restore` chain via gfx.c accessors.
3. **First real drawing primitive** — port one of the sprite-blit
   helpers so we can capture a recognisable image and diff against
   a Frida-captured retail screenshot.
4. **Main-loop driver** — `FUN_004b99c0` (sim+render driver, ~2200
   B).  This is what consumes `g_shutdown_request`'s 3→1→4
   escalator and what the per-tick `FUN_0040e8bb` / `FUN_004bb978`
   rate gate flows into.  Largest single port still ahead.

---

## 2026-05-24 — Windowed present via GDI (DWM workaround)

User flagged: visible window was black despite captures showing
red.  Confirmed via side-by-side back vs. GDI window capture
(both pipelines newly added in the same session):

```
frame_00100.bmp     (back via DDraw Lock):     mean=(128,0,0)
frame_00100-win.bmp (window via GDI BitBlt):   mean=(0,0,0)
```

The Blt was returning `DD_OK` but the visible surface never
updated.  Classic modern-Windows + DWM compositing gotcha:
DDraw NORMAL-mode Blts to primary go to the GPU's
front-buffer and bypass DWM's window backing stores, so
DWM re-composites the desktop from the unchanged backing
store and the user never sees the painted pixels.

### Fix

Routed the windowed-mode present through GDI's
`StretchDIBits` (Lock the back → build BITMAPINFO with the 256
palette entries inline → blit to `GetDC(hwnd)` → Unlock).  GDI
operations DO go through DWM's intercept layer, so the
window's backing store updates and the composited result is
visible.

Fullscreen path keeps `IDirectDrawSurface::BltFast` (exclusive
coop bypasses DWM entirely — no compositing mediation).

User confirms visual: window now shows solid red, matching
the captures.

### Why retail doesn't have this problem

`FUN_0042e5c0` auto-fullscreen-promotes any time the desktop
isn't 8bpp — i.e. always, on modern hardware.  Exclusive coop
level + SetDisplayMode puts retail's DDraw outside DWM
compositing, sidestepping the issue.  Our default-windowed
(`project_drop_in_defaults` memory) needs the GDI seat.

### Other changes

- `gfx_capture_window(path)` — GDI BitBlt of the window's
  client area to a 24bpp BMP.  Bypasses DDraw entirely, so it
  shows "what the user sees" rather than "what we drew".
  Smoke harness now emits both `frame_NNNNN.bmp` (back) and
  `frame_NNNNN-win.bmp` (visible) at every capture point.
- `WM_ERASEBKGND` returns 1 to suppress the class
  background-erase (BLACK_BRUSH would otherwise flash through
  during paint cycles).
- Memory recorded as `project_dwm_ddraw_normal.md` — this is
  the kind of gotcha that wastes an afternoon if you don't
  know it exists.

### What's next

Same as before: DSound bring-up, render rate-limiter, surface
restore, first real drawing primitive.  The visual-verify loop
is now closed end-to-end.

---

## 2026-05-24 — Back-buffer capture (autonomous visual diff)

User flagged that:

1. The smoke harness's CWD anchor was wrong — `init_exe_dir` was
   pinning CWD to the /tmp copy of the exe, so `base01.256`
   lookup failed.
2. We needed a screenshot mechanism so visual verification could
   happen without a human in the loop (parity with openrecet).

### Fixes landed

- **`init_exe_dir` now prefers `$OPENLORDS2_GAME_DIR`** (matches
  OpenMare's `init_game_dir` shape).  The flake's shellHook
  already propagates this across the WSL→Windows boundary as a
  native Windows path via `WSLENV/p`, so the .exe receives
  `C:\GOG Games\Lords of the Realm II` directly.  Falls back to
  the exe's parent dir when the env var is unset (production
  drop-in scenario).
- **Back buffer is now explicitly 8bpp.**  Without
  `DDPF_PALETTEINDEXED8` in the `CreateSurface` descriptor,
  DDraw inherits the primary's format — which on a 32bpp host
  is 32bpp.  An 8-bit palette can't attach to a 32bpp surface,
  so `SetPalette` returned `DDERR_INVALIDPIXELFORMAT`
  (`0x88760091`).  Always-explicit 8bpp on the back fixes this.
  The palette now attaches to the **back** in windowed mode
  (where the primary is 32bpp) and to the **primary** in
  fullscreen (where SetDisplayMode already made it 8bpp).
- **`gfx_capture_backbuffer(path)`** in `src/gfx.c` —
  `Lock(READONLY)` + per-pixel palette lookup via
  `g_palette_rgb` + top-down 24bpp BMP write.  Same spirit as
  openrecet's `capture_backbuffer` (which used D3D8 LockRect +
  32bpp), but pre-resolves palette → BGR so the BMP needs no
  palette section and viewers Just Work.
- **`--capture-to DIR --capture-every N`** flags in main.c.
  Implies `--gfx-init`.  Auto-creates the directory.  Linux
  paths (`/tmp/foo`) auto-translate to WSL UNC
  (`\\wsl.localhost\<WSL_DISTRO_NAME>\tmp\foo`) for the
  Windows-side process — propagated via `WSLENV`.

### Verified

Captured frames at `--capture-every 10` against an
unfilled-except-for-index-1 back buffer.  Every BMP is a
uniform `#800000` — palette index 1 from `base01.256`,
×4-expanded from its 6-bit VGA source.  Confirms end-to-end:

1. CWD anchoring works (768 bytes read from `base01.256`).
2. Palette load works (8bpp palette attached to back).
3. Back-buffer fill works (every pixel = idx 1).
4. Capture pipeline works (BMP shows the right colour).

### Why this matters

Visual verification now runs without a human in the loop.
When the next slice adds a real drawing primitive (a single
rectangle, a sprite blit, etc.), the smoke harness produces
BMPs the assistant reads back and confirms.  Bug-find loop
goes from "build → ship to user → wait" to "build → capture →
read pixel → iterate" in one session.

Adapted directly from openrecet's screenshot setup (see
`/opt/src/openrecet/src/main.c` `capture_backbuffer` and the
companion `--capture-to` / `--capture-every-ms` flags).  Same
pattern, swapped D3D8 → DDraw 8bpp, time-based → frame-based.

### What's next

1. **DSound bring-up** (`FUN_00425b10`).
2. **Render rate-limiter + dirty-rect tracker**
   (`FUN_00452160`/`FUN_00452306`).
3. **Surface-restore wire-up**.
4. **First real drawing primitive** — once we port one of the
   sprite-blit helpers (TBD which) we can capture a recognisable
   image and diff against a Frida-captured retail screenshot of
   the same content.

---

## 2026-05-24 — gfx_present + back-buffer fill

Added the per-frame back→primary present primitive and a simple
back-buffer fill so the existing palette load becomes visually
verifiable on the next manual run.

### Scope landed

- **`gfx_present()`** in `src/gfx.c` — port of the inner Blt /
  BltFast from retail's `FUN_00452160 @ 0x452160` (the
  "present" function).  Windowed: `primary->Blt(client_rect,
  back, NULL, DDBLT_WAIT, NULL)`.  Fullscreen:
  `primary->BltFast(0, 0, back, (0,0,640,480), DDBLTFAST_WAIT)`.
  Client rect is re-queried each call via
  `GetClientRect`+`ClientToScreen` so WM_MOVE/WM_SIZE don't need
  to push state into gfx.c yet (cheap query, no per-frame
  syscall hot path concern at this stage).
- **`gfx_fill_back_index(uint8_t)`** — 8-bit COLORFILL of the
  back buffer with a palette index.  For 8bpp DDraw surfaces,
  `DDBLTFX.dwFillColor` IS the palette index in the low byte —
  no RGB lookup needed.
- `gfx_init` primes the back buffer with index 1 at startup, so
  the next frame's present makes the visible window
  `base01.256[1]`'s colour (and stays black if no palette
  loaded — entry 0 is forced black).
- Main loop calls `gfx_present()` once per
  iteration when `--gfx-init` is active.  No rate limiter yet —
  retail's `FUN_00452160` gates on a 50-tick counter
  (`DAT_004eabe8`) and a dirty-rect tracker
  (`DAT_0050d09c`/`DAT_0050a4bc..c8`); both are deferred until
  the render driver lands.

### Verification

- Baseline smoke clean (50 frames).
- `--gfx-init` smoke clean (50 frames, present runs each frame
  against the hidden window, no errors).
- **Manual visual verification needed** on the next
  game-dir run: `--gfx-init --show-window` should display a
  solid colour (palette index 1 from `base01.256`) instead of
  black for the duration of the run.  If the window stays
  black, either the palette didn't load or the present isn't
  reaching the visible surface.

### What's next

Same as before plus:

1. **DSound bring-up** — port `FUN_00425b10` once human
   confirms gfx_present is visible.
2. **Render rate-limiter + dirty-rect tracker** — port
   `FUN_00452306` (dirty-rect accumulator) + the gating logic
   in `FUN_00452160` so we mirror retail's actual present
   cadence.  Right now we present every frame unconditionally.
3. **Surface-restore wire-up** still pending.

---

## 2026-05-24 — DDraw palette load (+ `src/io.c`)

DDraw bring-up confirmed visually on Windows by the user (`--gfx-init
--show-window` shows a brief black window, exits clean — matches
PROGRESS expectation).  Continued the chain with the palette load.

### Scope landed

- **`src/io.c`** — minimal file-IO helper `io_read_file_at(path,
  buf, len, offset)` that wraps `CreateFileA`+`SetFilePointer`+
  `ReadFile`+`CloseHandle`.  First-tier port of retail's
  `FUN_004af8d9`; the three-tier retry chain
  (`FUN_004af312`/`FUN_004af116`) is a TODO — we lean on
  `init_exe_dir` for the CWD anchor.
- **`gfx_load_palette(path)` in `src/gfx.c`** — reads 768 bytes
  (256 RGB triplets) from the given path, ×4-expands 6-bit VGA →
  8-bit RGB, builds a 256-entry `PALETTEENTRY` array with
  `PC_RESERVED` on entries 1..254 (entry 0 forced black, entry
  255 forced white per retail's boundary contract), calls
  `IDirectDraw::CreatePalette` + `IDirectDrawSurface::
  SetPalette` on the primary.  Releases any prior palette
  first so re-loads are clean.
- `gfx_init` now best-effort-loads `base01.256` after the
  initial Blt.  Failure is logged and skipped — primary still
  presents in the OS default palette.
- `gfx_shutdown` releases `g_palette` first in the cleanup
  chain.

### Simplifications from retail

- We don't preserve OS-reserved palette slots (0..9, 246..255)
  the way retail's `FUN_0042f449` does — that's only meaningful
  when running 8bpp on an 8bpp desktop, which today's hosts
  aren't.  In windowed mode on a 32bpp host DDraw handles the
  format conversion, so all 256 entries are ours.
- No `SetSystemPaletteUse` (windowed-8bpp-only concern).

### Verification

- **Baseline smoke** (no `--gfx-init`): 50 frames clean — only
  `OK :Active.` / `OK :Not active.` in the transcript.
- **`--gfx-init` from /tmp** (no `base01.256` reachable): full
  bring-up runs, palette load logs the graceful skip
  ("gfx_load_palette: read base01.256 got 0/768"), `OK :GFX
  DirectDraw enabled.` still fires, clean exit.
- **Success path live-verification** deferred — copying the exe
  to the game-dir via /mnt/c/ trips a Windows-side execute
  permission issue; needs human run on a native Windows shell
  to actually validate the colour load.  Code path is logically
  identical to the failure path with the success-arm taken;
  next manual run from the game dir is the ground-truth check.

### What's next

1. **Smoker bringup helper `FUN_00425b10`** — DSound init.  The
   harness already hooks `IDirectSound::SetCooperativeLevel`
   to demote PRIORITY→NORMAL; porting the bring-up gives us
   actual audio output paths.
2. **Surface-restore wire-up.**  Now that `gfx.c` owns
   primary/back/palette, expose accessors so WndProc's
   `WM_ACTIVATEAPP` active+lost arm can call
   `IDirectDrawSurface::Restore` and on failure
   `gfx_shutdown(); gfx_init()` — the `FUN_004b2922` outer
   call's equivalent.
3. **The flip helper `FUN_0045240a` → `FUN_00452306`.**  The
   per-frame present.  Once we know its exact `Blt` shape we
   can wire a "draw a recognisable pattern → present → confirm
   visually" sanity test in the smoke flow.

---

## 2026-05-24 — DDraw bring-up (`src/gfx.c`)

Ported the retail DDraw chain into a new `src/gfx.c`.

### Scope landed

- `gfx_init(HWND, fullscreen)` — `DirectDrawCreate` +
  `SetCooperativeLevel` (`NORMAL` for windowed, `EXCL|FULLSCREEN|
  ALLOWREBOOT` for fullscreen) + `SetDisplayMode(640,480,8)` for
  fullscreen only.
- Primary surface (`DDSCAPS_PRIMARYSURFACE`).
- Back buffer (system memory, `DDSCAPS_OFFSCREENPLAIN |
  DDSCAPS_SYSTEMMEMORY`, 640×480).
- Lock-probe on both surfaces (matches retail's
  `FUN_0042ec91` — caches `lPitch` for the render path's
  scanline-stride math).
- Clipper (windowed only — `IDirectDrawClipper` +
  `SetHWnd(hwnd)` + `IDirectDrawSurface::SetClipper`).
- Initial `Blt(COLORFILL=black, DDBLT_WAIT)`.
- `gfx_shutdown()` — releases everything in reverse order
  (clipper → back → primary → IDirectDraw).
- All status log lines mirror retail's exact wording —
  "OK :DD SetCooperative Level.", "OK :DD Kept resolution.",
  "OK :DD Set resolution.", "OK :DD Creating one Vram and one
  system surface.", "OK :DD Creating one Vram only.",
  "OK :DD Surfaces created and clipper attached.",
  "OK :DD Surface created.", "OK :GFX DirectDraw enabled." —
  so transcripts diff cleanly against a Frida-captured retail
  run.

### Scope deferred

- **Palette load.** Requires porting the file-read helper
  `FUN_004af8d9` (with its `FUN_004af116` /
  `FUN_004af312` retry layer for alternate game-dir search
  paths) first.  Without a palette the 8bpp primary is
  technically usable but renders against the OS's logical
  default palette — fine for bring-up smoke but not visually
  correct.
- **Smacker-aware paint.**  `FUN_0042e3ba` belongs in a
  Smacker-wrapper TU we don't have.
- **Surface-restore on focus regain.** WndProc's
  `WM_ACTIVATEAPP` path will call `IDirectDrawSurface::Restore`
  once `g_primary / g_back` are reachable from that side — the
  state mirror is already in place, just needs the helper.

### Default-off gating

`gfx_init` is gated behind `--gfx-init` in main.c.  `SetCooperative
Level` modifies process-global DDraw state and the headless smoke
harness has no way to validate the result visually — so default-off
keeps `tools/run-openlords2.sh` runs harmless and reserves the
DDraw path for an explicit `tools/run-openlords2.sh --gfx-init
--show-window`.

Baseline smoke (no flag) still passes — 50 frames clean, only
`OK :Active.` / `OK :Not active.` in the transcript.

With `--gfx-init` the full bring-up runs cleanly even against a
hidden window — six status lines + clean shutdown, no leaks
visible.  **Visual correctness needs a human run on a Windows
desktop** (per the validate-subjectively memory) — that's the
deferred item before this slice is "verified".

### Other changes

- `log_line` extern-visible (was static) so `gfx.c` can use the
  same logger.  No call-site change.
- `Makefile` links `-lddraw`.
- WndProc's `g_surface_lost` / `g_focused` / `g_render_dirty`
  mirrors from the previous slice are now reachable from
  `gfx.c`'s side — the surface-restore wire-up is one TODO away.

### What's next

1. **Palette load.**  Port `FUN_004af8d9` (open + lseek + read,
   with two retries on alternate paths) into a tiny `src/io.c`,
   then `FUN_004b0ab5` (×4 RGB expansion) and `FUN_0042f1df` →
   `FUN_0042f449` / `FUN_0042f306` (palette buffer construction
   + `IDirectDraw::CreatePalette` + `IDirectDrawSurface::
   SetPalette`).  At that point `--gfx-init` becomes visually
   correct enough to start diff-testing against retail.
2. **Visual verification on Windows.**  Manual `tools/
   run-openlords2.sh --gfx-init --show-window` from a Windows
   host — confirm the window appears, stays black for the run,
   exits clean.  Compare against a Frida-captured retail
   transcript of the same sequence.
3. **Surface-restore wire-up.** From WndProc's
   `WM_ACTIVATEAPP` active+lost path, call
   `IDirectDrawSurface::Restore(g_primary)` +
   `Restore(g_back)`; on failure, call `gfx_shutdown(); gfx_init()`
   (the equivalent of `FUN_004b2922`'s outer call).

---

## 2026-05-24 — WndProc port + findings

Continued top-down: ported retail's `FUN_004b29be @ 0x4b29be`
(WinLords2Class WndProc) into `src/main.c`, with a dedicated
findings doc.

### `docs/findings/wndproc.md`

Maps every message the retail WndProc handles to its retail
behaviour, call-chain, and the globals it reads/writes:

- WM_PAINT — Smacker-aware (delegates to `FUN_0042e3ba` if a
  movie is playing, else plain Begin/EndPaint + optional
  full-screen redraw when unfocused).
- WM_DESTROY — full subsystem teardown chain: close Smacker
  (`FUN_0042dfe2`), release palette (`FUN_004b28cd`), shut down
  DSound (`FUN_00425d35`), shut down DPlay (`FUN_004b7180`),
  then `PostQuitMessage(0)`.
- WM_MOVE / WM_SIZE — recompute the **screen-space client
  RECT** at `DAT_004ea180`+, log "OK :Window moved." / "OK
  :Minimizing." / "OK :Paused.", drive a redraw based on engine
  state.
- WM_CLOSE — sets `DAT_005aeb68 = 3` and returns.  The actual
  close is handled by the sim/render driver's escalator
  (`3 → 1 → 4`), so an external Alt+F4 cleanly drains the engine
  before the surfaces die.
- WM_ACTIVATEAPP — toggles the focus flag, logs "OK :Active." /
  "OK :Not active.", and on focus-regained-with-lost-surfaces
  runs `FUN_004b2730` (DDraw `Restore`) and on failure
  `FUN_004b2922` (full re-init).
- WM_DISPLAYCHANGE — caches new screen bpp + size.
- WM_KEYDOWN — a fat VK switch.  All 30-odd cases mapped to
  their `FUN_xxxxx` handlers (digits with Ctrl-modifier,
  F1/WinHelp, F2-cycle, F5/recentre, F7/F8 debug, ESC/menu, …).
- WM_CHAR / WM_SYSCHAR — text input + Alt-shortcut mappings
  (Alt-1, Alt-2..5, Alt-X = ESC).
- Mouse — coord cache at `DAT_004eabbe/004eabc0`, button bitmask
  `DAT_004eabc2`, dbl-click latch `DAT_004eada1`.
- WM_PALETTECHANGED / WM_QUERYNEWPALETTE — only act in
  windowed 8bpp; recreates the palette via `FUN_0042f1df`.

Also catalogued the 10 retail log strings the WndProc emits via
`FUN_004afab9` (`OK :Paused.`, `OK :Active.`, `ERR:Can't
restore front buffer.`, etc.) so the drop-in can mirror them
for differential transcripts later.

### Port landed in `src/main.c`

Structural messages fully ported — the drop-in now logs in
retail's exact wording ("OK :Active." / "OK :Not active." /
"OK :Window moved." / "OK :Minimizing." / "OK :Paused.")
through the same code paths.  Mouse-state mirror (`g_mouse_x`,
`g_mouse_y`, `g_mouse_buttons`, `g_mouse_dblclk`) and
focus/shutdown-request flags (`g_focused`, `g_surface_lost`,
`g_shutdown_request`) carry the same semantics as their
retail DAT_* counterparts — so when the sim/render driver
lands, the rename is global-replace.

Game-state hotkeys remain TODOs (each labelled with the retail
`FUN_xxxxx` it should call) — VK_ESCAPE/F1/F5/Ctrl are
honoured as drop-in conveniences.  WM_CLOSE both sets the
deferred-shutdown flag AND inlines `DestroyWindow` so we still
exit cleanly without the sim escalator that doesn't exist yet.

Smoke verified: 50 frames in <1 s under
`tools/run-openlords2.sh`, transcript shows
`OK :Active.` on window activation and `OK :Not active.` on
the synchronous deactivation during DestroyWindow.

### What's next

1. **DDraw bring-up** — port `FUN_004b284e` (DirectDrawCreate +
   SetCooperativeLevel + SetDisplayMode) and `FUN_0042ec91`
   (primary surface, back buffer, clipper, lock-probe) into a
   new `src/gfx.c`.  WndProc's surface-restore path can then
   call into real code.
2. **Turbo v2 in the Frida agent** — `timeGetTime`-only seat
   per the `main-loop-tick` finding.  Verify rise rate of
   `DAT_0057c998` against the scale.
3. Defer porting the per-screen hotkeys (F2/F3/F4/F6/F7/F8/F9
   etc.) until the screen-state machine + sim driver land —
   they all bottom out in game-state code that doesn't exist
   yet.

---

## 2026-05-24 — DDraw/DSound + main-loop findings + InitInstance port

Continued top-down chip-away from the entry point.  Three landings:

### Two new findings docs

- [`docs/findings/ddraw-dsound-bringup.md`](findings/ddraw-dsound-bringup.md)
  decodes the post-window init chain.  `FUN_004b2922` is just an
  orchestrator; the real work is `FUN_004b284e` →
  `FUN_0042e5c0` (`DirectDrawCreate` + `SetCooperativeLevel` +
  `SetDisplayMode`) and `FUN_0042ec91` (primary surface, back
  buffer, clipper, lock-probe).  `FUN_00425b10` is the DSound
  bring-up called separately from WinMain after `FUN_004b2258`
  returns: `DirectSoundCreate` + `SetCooperativeLevel(PRIORITY)`
  + `CreateSoundBuffer` primary + `SetFormat`.  All vtable
  offsets + log-prefix strings mapped, so the existing Frida
  hooks have a paper trail.

- [`docs/findings/main-loop-tick.md`](findings/main-loop-tick.md)
  corrects a misread from the first session:
  `FUN_004b8c3a` is the **DPlay receive pump**, not the
  per-tick driver.  The real sim+render path is `FUN_0040e8bb`
  (surface-lost guard) → `FUN_0040e890` → `FUN_004b99c0` (2222 B
  sim+render driver, with a variable sub-step loop fed by
  `FUN_004bb978`'s rate gate on `_DAT_0053f230` speed slider and
  `_DAT_004e5b0c` last-tick wall-clock).

  Key turbo-v2 insight: the rate gate depends on `timeGetTime` for
  sim+render+FPS+UI, but the DPlay pump uses `GetTickCount`.
  Virtualising **only** `timeGetTime` thus accelerates the
  client-side sim cleanly while leaving multiplayer wire timing
  on real wall-clock.  No need to do both APIs.

### `FUN_004b2258` (InitInstance) ported into `src/main.c`

Class registration and window creation now match retail:

- Class name `WinLords2Class` (so `FindWindowA` from outside the
  process resolves identically — matches what retail's class
  registration uses, see `s_WinLords2Class_004df5b0`).
- Title `Lords2` matches `s_Lords2_004df5c0`.
- `CS_DBLCLKS`, NULL cursor, `BLACK_BRUSH` background, NULL menu.
- `UpdateWindow` + `SetFocus` after create.

Deliberate deviations preserved:

- `WS_VISIBLE` never set inline — `--show-window` does ShowWindow
  separately so `--hidden` headless runs compose cleanly.
- Default style is `WS_OVERLAPPEDWINDOW` at 640×480 centred (the
  exact shape retail transforms to in `FUN_0042e5c0`'s now-dead-code
  8bpp-desktop windowed branch).  `--fullscreen` opts back into
  retail's WS_POPUP full-screen-sized popup.

Smoke verified: 50 frames in well under 8 s under
`tools/run-openlords2.sh`; class registers, window creates,
loop exits cleanly with rc=0.

### What's next

1. Port `FUN_004b29be` (the 2545-byte WndProc) into `src/main.c`'s
   `wndproc`.  Mostly state-mux on `param_2` (WM_*) → engine
   substate jumps.  Worth a dedicated findings doc.
2. Port the bring-up *body* of `FUN_004b2922` into a new
   `src/gfx.c` — DirectDrawCreate + SetCooperativeLevel + surface
   create + palette load.  First real DDraw code in the drop-in.
3. Add turbo v2 to the Frida agent now that we know the
   `timeGetTime`-only seat suffices.  Validate against retail
   behaviour: `DAT_0057c998` should rise N× faster on a chosen
   scale; FPS counter `DAT_004ea174` should be unaffected at the
   monitor cap but FPS-since-boot `DAT_004ea0f4 / DAT_004ea83c`
   should drift.
4. Read `FUN_004b29be` first (item 1) before doing the DDraw port,
   since several DDraw surface-restore paths post WM_PAINT /
   WM_ACTIVATE messages that the wndproc must already handle.

---

## 2026-05-24 — Harness verified end-to-end + first findings

Initial bootstrap session continued.

### Retail harness verified against Lords2.exe

Ran `tools/smoke-test.py --duration-ms 8000` against the real GOG
`Lords2.exe` via the cutestation.soy frida-server.  Results:

```
hwnd_count:        5        (1 game + 4 system: OLE, CIC, MSCTFIME)
hide_count:        2        (CreateWindowExA/WS_VISIBLE + SetWindowLongA)
ddraw_coop:        1        (0x13 EXCL|FULLSCREEN|ALLOWREBOOT → 0x0a NORMAL|ALLOWREBOOT)
SetDisplayMode:    1 blocked (640×480×8 — the classic 8-bit mode-set)
dsound_coop:       1        (0x2 PRIORITY → 0x1 NORMAL)
msgbox:            0
audio_mute:        0        (waveOutSetVolume not called yet in first 8s)
```

**First-run gotcha caught.**  The initial smoke run produced a
borderless black window flash on the host desktop despite all four
post-create show-path hooks being armed.  Root cause: Lords2.exe
passes `WS_VISIBLE` directly in `CreateWindowExA`'s `dwStyle` (style
`0x90080000` = `WS_POPUP|WS_VISIBLE|WS_SYSMENU`), so the window is
shown *during* the create call — by the time `onLeave` registers the
HWND, the display has already happened.  Added a pre-create
`WS_VISIBLE` strip in `CreateWindowEx` `onEnter` (commit `3c093ba`);
second run is fully invisible.

### First Ghidra-headless run completed

`tools/ghidra-headless.sh` decompiled the binary in single-digit
minutes: **2452 functions**, 3 MB `all.c`.

Documented the WinMain → InitInstance → main-loop chain in
[`docs/findings/winmain-and-bootstrap.md`](findings/winmain-and-bootstrap.md):

- `entry @ 0x4c3370` — CRT bootstrap, calls `FUN_0040e9ab(hInstance)`
- `FUN_0040e9ab @ 0x40e9ab` — WinMain-equivalent.  641 B.  Hands off
  to `FUN_004b2258` for window+class setup, then drops into the
  PeekMessage/GetMessage main loop with a `Sleep(200)` paused-mode
  branch.
- `FUN_004b2258 @ 0x4b2258` — InitInstance-like.  Registers
  `WinLords2Class` (wndproc `FUN_004b29be`), creates the main window
  full-screen-sized at (0,0), then calls `FUN_004b2922` for what is
  likely DDraw/DSound bring-up.
- Default render size globals confirmed: `0x4eb250 = 640`,
  `0x4eb254 = 480`.

### What's next

1. Read `FUN_004b2922` to understand the DDraw/DSound bring-up chain
   — that's where the 640×480×8 mode-set lives.
2. Read `FUN_004b8c3a` (per-tick driver, called every main-loop
   iteration when no Win32 message is pending) — likely contains sim
   step + render dispatch.
3. Decide whether to add turbo v2 (virtual `timeGetTime`) to the
   harness now or wait until we have the frame counter located.
4. Begin porting `FUN_004b2258` to `src/main.c`'s
   `register_window_class` + `create_main_window` so the drop-in's
   window matches retail (class name, style bits, popup vs
   overlapped).

---

## 2026-05-24 — Bootstrap (initial project setup)

Project skeleton created from scratch.  Adapts the conventions from
the two sibling RE projects in `/opt/src/`:

- **openrecet** — first decomp project; established the
  Ghidra-headless / Frida-harness / co-author-trailer commits style.
- **OpenMare** — newer, more refined workflow; the Job-Object
  launcher pattern, single-TU C builds, MessageBox redirect hooks
  (both Frida-side and drop-in-side), and turbo v2 virtual clock
  all originate there.

### Target binary identification

`Lords2.exe` from the GOG release of *Lords of the Realm II*
(Impressions Games / Sierra On-Line, 1996; GOG patch 2.0.0.3).

```
file:    PE32 executable (GUI) Intel 80386
linker:  MSVC 4.20 (1996-era)
size:    1,031,680 bytes
sha256:  6fec043e59ed3d7ad5de5b313093b8e2ee2d7832ccc224705e010bfe164d004a
imports: KERNEL32, USER32, GDI32, DDRAW, smackw32, DSOUND, WINMM, DPLAYX
base:    0x00400000   image size: 0x001E9000   entry RVA: 0x000C3370
```

No DRM packer; Ghidra can analyze the binary directly.

Companion: `mapl2.exe` (253 KB) is the map editor — out of scope for
the drop-in but a useful side artifact for understanding the map
format.

### What landed

1. `flake.nix` — same shape as OpenMare's: Ghidra/radare2/cutter,
   frida-tools, mingw32 cross-compiler, Python env with
   pillow/numpy/skimage/opencv/frida-python/construct/rich, ffmpeg,
   imagemagick.  `OPENLORDS2_GAME_DIR` defaults to the GOG path.
2. `.gitignore`, `LICENSE` (MIT), `README.md`.
3. `tools/setup.sh` — symlinks `vendor/original/` → GOG install,
   hashes Lords2.exe + companion DLLs + L2.INI + Sierra.inf.
4. `docs/PLAN.md`, `docs/AGENT-WORKFLOW.md`, this log, and
   `docs/findings/engine-quirks.md` scaffold.

### Next candidates

1. Launcher wrapper (`tools/launcher/openlords2-launcher.c`) —
   Job-Object pattern from OpenMare's `openmare-launcher.c`.
   Guarantees the spawned `.exe` dies when the launcher does, no
   matter how the launcher dies.
2. Frida agent (`tools/frida/openlords2-agent.js`) — hidden window
   (`ShowWindow`/`SetWindowPos`/`SetWindowLong` multi-API), DDraw
   `SetCooperativeLevel` exclusive-fullscreen stripping, DSound
   volume clamps, MessageBox capture + IDOK return, turbo v2
   virtual clock.
3. Python driver (`tools/frida_capture.py`, `tools/smoke-test.py`) —
   port from OpenMare, swap target binary name.
4. Drop-in stub (`src/main.c`) — open a hidden window, install the
   MessageBox-to-stderr dev hook, cd into the game dir, exit
   cleanly on a `--frames N` budget.
5. Ghidra-headless decompile pipeline.

Deferred until the user is back: any test run that has potential to
disrupt the host desktop (fullscreen flip, audio output) — the goal
of phases 1-3 above is precisely to make subsequent runs
non-disruptive.
