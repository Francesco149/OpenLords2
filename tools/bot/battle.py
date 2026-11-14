"""battle.py — the bot takes the field: an interactive battle on retail's battlefield (screens 0x28/0x29/0x2a/0x2b).

Learned live in rhost 2026-10-23 (bot v3; tools/bot/README.md "Retail's input model"):
- the pre-battle scroll 0x12 ("A Battle is to be fought. Will you take the field?"): the LEFT gauntlet (345,128)
  takes the field, the right one (383,128) auto-resolves (FUN_0043b622). Taking the field lands on 0x29 PAUSED
  ("Battle Paused", DAT_0053f238 = -1) with the camera at hex (32,33) of the 80x80 field;
- the control panel (table 0x4dc710, origin (480,448), 32-px buttons): hourglass pause toggle FUN_0043b9a1
  (496,464); retreat FUN_0043ba29 (528,464) → confirm 12 "retreat from field?" (11 "surrender castle?" when we
  defend a besieged castle); siege-defender action FUN_0043bbe7 (560,464); "Mop up enemy troops" FUN_0043bd02
  (592,464): every unit of ours charges (action 8); autocalc FUN_0043bd67 (624,464) → confirm 9 "autocalc
  battle?". The confirm dialog's thumbs sit where the strategic one's do: yes (240,218), no (287,218);
- the field: 32-px hexes from (DAT_004e6558, DAT_004e6554) = (0,24), 15x14 visible, camera (col,row) at
  DAT_0055ce70 / DAT_005678a0; units 1..80 at 0x5544ac + i*0x1b0 (+0 owner, -0x0c/-0x0a i16 col/row,
  -0x1a class, +5 action);
- the battlefield minimap (480..639, 24..183, 2 px a hex; FUN_00432443): an LMB there RECENTRES the camera when
  nothing is selected or the battle is paused, and otherwise ORDERS the selection to march to that hex;
- selection: an LMB drag over the field (> 24 px, FUN_00479cf7) box-selects (screen 0x2a while held) — it works
  while paused; DAT_00553078 = units selected;
- an order: an LMB press + release over a hex WITH a selection (FUN_0043c57d → FUN_0043c634 → FUN_00479e90), only
  while running (not paused); an enemy under the cursor (DAT_005533b8) makes it an attack on that unit (missile
  units fire at it: 0x479e90 → 0x48143b). An RMB release DESELECTS (bot v3 sent RMB orders: none was ever given —
  found authoring tests/scenarios/field-orders, 2026-11-04);
- the end: men left per side DAT_0053f028 (attacker = piece DAT_00568210) / DAT_00553c58 (defender = piece
  DAT_00568218); FUN_00477dfc checks every frame and at 0 switches to 0x2b ("The Battle is won/lost" scroll,
  FUN_00478419 picks the line); an RMB there ends it at once (else ~2500 frames), then aftermath → 0x13.
  Retreat / autocalc go through FUN_0043be65 instead.
"""
import struct

import l2state

UNIT, USTRIDE, NUNITS = 0x5544AC, 0x1B0, 80
CAM_COL, CAM_ROW = 0x55CE70, 0x5678A0
GRID_X0, GRID_Y0, CELL = 0x4E6558, 0x4E6554, 0x4E656C
PAUSED = 0x53F238                  # 0 running, -1 frozen
MEN_ATK, MEN_DEF = 0x53F028, 0x553C58
ATK_PIECE, DEF_PIECE = 0x568210, 0x568218
SELECTED = 0x553078
UNDER_CURSOR = 0x5533B8            # enemy unit under the cursor (an LMB with a selection attacks it)
FIELD_SCREENS = (0x28, 0x29, 0x2A, 0x2B)
TAKE_FIELD = (345, 128)            # 0x12: the left gauntlet
HOURGLASS, RETREAT, SIEGE_ACT, MOP_UP, AUTOCALC = (496, 464), (528, 464), (560, 464), (592, 464), (624, 464)
BCONFIRM_YES, BCONFIRM_NO = (240, 218), (287, 218)
MINIMAP_X0, MINIMAP_Y0 = 480, 24
VIEW = (0, 24, 479, 471)           # the field's screen rectangle (15x14 hexes of 32 px)


def units(s):
    """the battlefield's live units → [{i, owner, col, row, cls, act}]"""
    raw = s.read(UNIT - 0x30, USTRIDE * (NUNITS + 1) + 0x30)
    out = []
    for i in range(1, NUNITS + 1):
        b = i * USTRIDE + 0x30
        if raw[b] == 0:
            continue
        col, row = struct.unpack_from("<hh", raw, b - 0x0C)
        out.append({"i": i, "owner": raw[b], "cls": raw[b - 0x1A], "col": col, "row": row, "act": raw[b + 5]})
    return out


def men(s):
    """(ours, theirs, we_attack) — men left per side"""
    me = s.i32(l2state.LOCAL_PLAYER)
    atk = s.i32(ATK_PIECE)
    we_attack = s.read(l2state.PIECE_BASE + atk * l2state.PIECE_STRIDE, 1)[0] == me
    a, d = s.i32(MEN_ATK), s.i32(MEN_DEF)
    return (a, d, True) if we_attack else (d, a, False)


def to_screen(s, col, row):
    x0, y0, cell = s.i32(GRID_X0), s.i32(GRID_Y0), s.i32(CELL) or 32
    return (col - s.i32(CAM_COL)) * cell + x0 + cell // 2, (row - s.i32(CAM_ROW)) * cell + y0 + cell // 2


def on_view(p, margin=4):
    return VIEW[0] + margin <= p[0] <= VIEW[2] - margin and VIEW[1] + margin <= p[1] <= VIEW[3] - margin


def minimap(s, col, row):
    """recentre the battlefield view on (col, row) via the minimap. In multiplayer the camera moves frames later
    (inputs go through the MP action pipeline) — wait until it has moved (≤ 60 frames) before callers read it."""
    x, y = MINIMAP_X0 + 2 * col + 1, MINIMAP_Y0 + 2 * row + 1
    cam = (s.i32(CAM_COL), s.i32(CAM_ROW))
    s.move(x, y, frames=4)
    s.click(x, y)
    s.run(6)
    for _ in range(27):
        if (s.i32(CAM_COL), s.i32(CAM_ROW)) != cam:
            s.run(2)
            break
        s.run(2)


def drag(s, x0, y0, x1, y1, steps=8):
    """press at (x0,y0), move with the button held, release at (x1,y1)"""
    s.post(0x200, 0, (y0 << 16) | x0); s.run(3)
    s.post(0x201, 1, (y0 << 16) | x0); s.run(3)
    for k in range(1, steps + 1):
        x, y = x0 + (x1 - x0) * k // steps, y0 + (y1 - y0) * k // steps
        s.post(0x200, 1, (y << 16) | x); s.run(2)
    s.post(0x202, 0, (y1 << 16) | x1); s.run(6)


def set_paused(s, paused):
    if (s.i32(PAUSED) != 0) != paused:
        s.move(*HOURGLASS, frames=4)
        s.click(*HOURGLASS)
        s.run(6)
    return s.i32(PAUSED) != 0


def centroid(us):
    return sum(u["col"] for u in us) // len(us), sum(u["row"] for u in us) // len(us)


def confirm(s, yes=True):
    """answer the battle's confirm dialog (0x1e) → True when the battle left the field"""
    for _ in range(20):
        if s.screen == 0x1E:
            break
        s.run(3)
    if s.screen != 0x1E:
        return False
    pt = BCONFIRM_YES if yes else BCONFIRM_NO
    s.move(*pt, frames=4)
    s.click(*pt)
    s.run(20)
    return s.screen not in FIELD_SCREENS + (0x1E,)


def densest(us, half_w=6, half_h=5):
    """the hex centring the view on the most of `us` (a defender's units deploy in groups far apart: their
    centroid can fall between them, with none in view)"""
    best = max(us, key=lambda c: sum(1 for u in us if abs(u["col"] - c["col"]) <= half_w and abs(u["row"] - c["row"]) <= half_h))
    near = [u for u in us if abs(u["col"] - best["col"]) <= half_w and abs(u["row"] - best["row"]) <= half_h]
    return centroid(near)


def select_all(s, me):
    """recentre on the largest group of our units (paused) and box-select the ones in view → units selected"""
    mine = [u for u in units(s) if u["owner"] == me]
    if not mine:
        return 0
    minimap(s, *densest(mine))
    pts = [to_screen(s, u["col"], u["row"]) for u in mine]
    pts = [p for p in pts if on_view(p, 0)] or pts
    x0, y0 = max(VIEW[0] + 2, min(p[0] for p in pts) - 20), max(VIEW[1] + 2, min(p[1] for p in pts) - 20)
    x1, y1 = min(VIEW[2] - 2, max(p[0] for p in pts) + 20), min(VIEW[3] - 2, max(p[1] for p in pts) + 20)
    drag(s, x0, y0, x1, y1)
    return s.i32(SELECTED)


def attack_nearest(s, me):
    """recentre on the nearest enemy (paused), run, put the cursor on it and LMB (the selection is ordered) →
    'attack' / 'move' / None"""
    us = units(s)
    mine = [u for u in us if u["owner"] == me]
    foes = [u for u in us if u["owner"] != me]
    if not mine or not foes:
        return None
    cx, cy = densest(mine)
    foe = min(foes, key=lambda u: (u["col"] - cx) ** 2 + (u["row"] - cy) ** 2)
    set_paused(s, True)
    minimap(s, foe["col"], foe["row"])           # paused: recentres instead of ordering
    set_paused(s, False)
    for _ in range(3):                           # the target walks: aim at its live hex, confirm the hover
        u = next((x for x in units(s) if x["i"] == foe["i"]), None)
        if u is None:
            break
        p = to_screen(s, u["col"], u["row"])
        if not on_view(p):
            break
        s.move(*p, frames=4)
        if s.i32(UNDER_CURSOR):
            s.click(*p)
            s.run(6)
            return "attack"
    p = to_screen(s, foe["col"], foe["row"])     # no unit under the cursor: a move order onto its hex
    if on_view(p):
        s.move(*p, frames=4)
        s.click(*p)
        s.run(6)
        return "move"
    return None


def fight(s, ui, tactic="attack", max_frames=24000, check=150, log=print):
    """play the battle on the field from 0x28/0x29 to its end screen → a record.
    tactic: "attack" (select all, attack the nearest enemy, mop up when idle), "mopup" (only the Mop-up button),
    "retreat" (attack, then retreat once losing: ours < 60 % of the start and fewer than theirs), "autocalc"
    (attack, then hand the rest to the auto-calculation after a short skirmish)."""
    me = s.i32(l2state.LOCAL_PLAYER)
    f0 = s.frame
    for _ in range(40):                          # the deploy phase 0x28 starts on an RMB
        if s.screen in (0x29, 0x2A):
            break
        if s.screen == 0x28:
            s.click(240, 240, right=True)
        s.run(5)
    start = men(s)
    rec = {"frame": f0, "what": "battle", "tactic": tactic, "start": list(start[:2]), "we_attack": start[2]}
    if s.screen not in (0x29, 0x2A):
        rec["result"] = f"no field ({s.screen:#x})"
        return rec
    set_paused(s, True)
    rec["selected"] = select_all(s, me)
    if tactic == "mopup":
        set_paused(s, False)
        s.move(*MOP_UP, frames=4); s.click(*MOP_UP); s.run(6)
        rec["order"] = "mopup"
    else:
        rec["order"] = attack_nearest(s, me)
        if rec["order"] is None:
            set_paused(s, False)
            s.move(*MOP_UP, frames=4); s.click(*MOP_UP); s.run(6)
            rec["order"] = "mopup"
    mopped, ended_by, last = rec["order"] == "mopup", None, start
    n = 0
    while s.screen in FIELD_SCREENS + (0x1E,) and s.frame - f0 < max_frames:
        s.run(check)
        n += 1
        if s.screen == 0x2B:                    # "The Battle is won / lost": an RMB ends it now
            last = men(s)
            ended_by = ended_by or "field"
            s.click(240, 240, right=True)
            s.run(20)
            continue
        if s.screen not in (0x29, 0x2A):
            continue
        last = men(s)
        ours, theirs, _ = last
        if tactic == "retreat" and ours < 0.6 * start[0] and ours < theirs:
            s.move(*RETREAT, frames=4); s.click(*RETREAT); s.run(6)
            if confirm(s, True):
                ended_by = "retreat"
            continue
        if tactic == "autocalc" and n >= 8:
            s.move(*AUTOCALC, frames=4); s.click(*AUTOCALC); s.run(6)
            if confirm(s, True):
                ended_by = "autocalc"
            continue
        if not mopped and n % 6 == 0:            # the fight drifts apart: every unit charges
            if s.i32(PAUSED):
                set_paused(s, False)
            s.move(*MOP_UP, frames=4); s.click(*MOP_UP); s.run(6)
            mopped = True
    if s.screen in FIELD_SCREENS:                # out of time: hand it to the auto-calculation
        set_paused(s, False)
        s.move(*AUTOCALC, frames=4); s.click(*AUTOCALC); s.run(6)
        if confirm(s, True):
            ended_by = "autocalc (timeout)"
        s.run(30)
    rec.update({"end": list(last[:2]), "ended_by": ended_by or f"screen {s.screen:#x}", "frames": s.frame - f0,
                "screen": s.screen})
    log(f"  battle: {tactic} {rec['start']} → {rec['end']} ({rec['ended_by']}, {rec['frames']} frames, order {rec['order']}, "
        f"selected {rec['selected']})")
    return rec
