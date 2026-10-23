"""actions.py — semantic actions for the autoplay bot, compiled to UI input and checked against state.

Every action is a sequence of clicks through rhost's virtual queue (so it lands in the trace), with the
timing retail's mouse model needs: an in-game LMB click fires 300 ms after release and only when no UI
element consumed the press (engine_input.c, DAT_004e65f0), so map clicks are spaced >= 25 frames.
Coordinates verified in rhost 2026-10-23 (docs/findings/game-state-map.md, tools/bot/mapview.py).
"""
import l2state
import mapview

GAP = 25                                   # frames between map clicks (> 300 ms at 17 ms/frame)
DWELL = 25                                 # frames the cursor rests on a destination first: retail
                                           # computes the move-path preview on mouse idle and ignores
                                           # a destination click without one
RAISE_ICON, SLIDER_MORE, CONTINUE, CREATE = (492, 447), (310, 201), (490, 346), (588, 410)
RATION_UP, RATION_DOWN = (356, 141), (380, 141)
BACK = (300, 240)


def _dismiss_hint(s):
    if s.screen == 0x27:
        s.click(*BACK, right=True)
        s.run(GAP)


def _mine(s):
    st = l2state.snapshot(s.read)
    return st, [a for a in st["armies"] if a["owner"] == st["me"] and a.get("kind", "army") == "army"]


def raise_army(s, ui, men):
    """draft about `men` peasants in the selected county (slider ~4-5 men a click) → the new army or None"""
    st0, before = _mine(s)
    s.click(*RAISE_ICON); s.run(30)
    _dismiss_hint(s)
    if s.screen != 0x17:
        ui.settle()
        return None
    for _ in range(max(1, min(120, round(men / 4.4)))):
        s.click(*SLIDER_MORE, frames=2)
    s.click(*CONTINUE); s.run(30)
    _dismiss_hint(s)
    s.click(*CREATE); s.run(40)
    ui.settle()
    st, after = _mine(s)
    new = [a for a in after if a["id"] not in {b["id"] for b in before}]
    return new[0] if new else None


def select_army(s, army, st):
    """make `army` visible and click it → True when retail entered move mode (screen 0x10)"""
    home = next((c for c in st["counties"] if c["id"] == army["county"]), None)
    pt = mapview.locate(s, *army["pos"])
    if pt is None and home is not None:
        s.click(*home["minimap_click"]); s.run(GAP)
        pt = mapview.locate(s, *army["pos"])
    if pt is None and mapview.scroll_to(s, *army["pos"]):
        pt = mapview.locate(s, *army["pos"])
    if pt is None:
        return False
    s.click(*pt); s.run(GAP)
    if s.screen == 0x27:                   # the one-time "Army Movement" hint
        _dismiss_hint(s)
        s.click(*pt); s.run(GAP)
    return s.screen == 0x10


def march(s, ui, army, target_cell):
    """order `army` toward logical cell `target_cell` (a county's town to claim / attack it)"""
    st = l2state.snapshot(s.read)
    if not select_army(s, army, st):
        return "cannot select"
    if not mapview.scroll_to(s, *target_cell):
        s.click(*BACK, right=True); s.run(GAP)
        return "target not reachable on screen"
    pt = mapview.locate(s, *target_cell)
    s.move(*pt, frames=DWELL)
    s.click(*pt); s.run(GAP)
    if s.screen == 0x10:                   # still selected: the destination was refused (no path?)
        s.click(*BACK, right=True); s.run(GAP)
        return "refused"
    st2, mine = _mine(s)
    a2 = next((x for x in mine if x["id"] == army["id"]), None)
    if a2 is not None and list(a2["dest"]) == list(target_cell):
        return "ordered"
    return f"screen {s.screen:#x}"


def set_rations(s, ui, level, read_level, max_clicks=8):
    """rations panel arrows until read_level() == level (index into l2state.RATIONS)"""
    ui._panel((600, 218), 0x19)
    for _ in range(max_clicks):
        cur = read_level()
        if cur == level:
            break
        s.click(*(RATION_UP if level > cur else RATION_DOWN), frames=4)
    ui.close()
    return read_level()


# ---- the merchant (verified 2026-10-23 by buying 5 of each and diffing the state) ----
MARKET = {"cattle": (50, 180), "grain": (215, 320), "wood": (470, 200), "stone": (300, 295), "iron": (175, 410),
          "maces": (335, 375), "swords": (380, 410), "crossbows": (500, 410), "armour": (580, 425), "bows": (330, 460)}
QTY_UP, QTY_DOWN, CONFIRM = (155, 227), (180, 227), (338, 262)   # one unit per click; "Complete this …?" yes


def trade(s, ui, good, qty):
    """buy (qty > 0) or sell (qty < 0) at the merchant of the selected county → what moved, or a reason"""
    st = l2state.snapshot(s.read)
    m = next((x for x in st["merchants"] if x["county"] == st["selected_county"]), None)
    if m is None or good not in MARKET:
        return "no merchant here"
    pt = mapview.locate(s, *m["pos"])
    if pt is None:                                   # recentre: the minimap point of the county, then edge-scroll
        home = next((c for c in st["counties"] if c["id"] == st["selected_county"]), None)
        if home and home.get("minimap_click"):
            s.click(*home["minimap_click"]); s.run(GAP)
        pt = mapview.locate(s, *m["pos"])
        if pt is None and mapview.scroll_to(s, *m["pos"]):
            pt = mapview.locate(s, *m["pos"])
    if pt is None:
        return "merchant not on screen"
    s.move(*pt, frames=DWELL); s.click(*pt); s.run(30)
    _dismiss_hint(s)
    if s.screen != 0x08:
        ui.settle(max_frames=600)
        return f"market did not open ({s.screen:#x})"
    s.click(*MARKET[good]); s.run(30)
    _dismiss_hint(s)
    if s.screen != 0x0c:
        s.click(*BACK, right=True); s.run(GAP); ui.settle(max_frames=600)
        return f"no trade screen ({s.screen:#x})"
    for _ in range(min(60, abs(qty))):
        s.click(*(QTY_UP if qty > 0 else QTY_DOWN), frames=3)
    s.run(10)
    s.move(*CONFIRM, frames=6); s.click(*CONFIRM); s.run(30)
    for _ in range(3):
        if s.screen == 0:
            break
        s.click(*BACK, right=True); s.run(GAP)
    ui.settle(max_frames=600)
    return "done"


# ---- diplomacy: a gift to a noble (Noble Faces → the noble → +10 crowns a click → dispatch) ----
DIPLOMACY_ICON, NOBLE, GIFT_MORE, DISPATCH = (622, 447), (415, 118), (200, 255), (303, 293)


def gift(s, ui, crowns):
    s.click(*DIPLOMACY_ICON); s.run(30)
    _dismiss_hint(s)
    if s.screen != 0x0b:
        ui.settle(max_frames=600)
        return f"diplomacy did not open ({s.screen:#x})"
    s.click(*NOBLE); s.run(30)
    _dismiss_hint(s)
    for _ in range(max(1, min(50, crowns // 10))):
        s.click(*GIFT_MORE, frames=3)
    s.run(10)
    s.click(*DISPATCH); s.run(30)
    for _ in range(3):
        if s.screen == 0:
            break
        s.click(*BACK, right=True); s.run(GAP)
    ui.settle(max_frames=600)
    return "sent"


# ---- castles (screen 0x1b): the design strip, then "Start construction?" yes (verified 2026-10-23) ----
CASTLE_ICON = (592, 447)
CASTLE_DESIGNS = [(60, 340), (145, 345), (250, 320), (360, 360), (530, 330)]   # palisade .. royal castle
BUILD_YES = (447, 455)


def build_castle(s, ui, level):
    """start building castle design `level` (1 palisade .. 5 royal) in the selected county"""
    if not 1 <= level <= 5:
        return "no such design"
    s.click(*CASTLE_ICON); s.run(40)
    _dismiss_hint(s)
    if s.screen != 0x1b:
        ui.settle(max_frames=600)
        return f"castle screen did not open ({s.screen:#x})"
    s.click(*CASTLE_DESIGNS[level - 1]); s.run(30)
    s.move(*BUILD_YES, frames=6); s.click(*BUILD_YES); s.run(40)
    for _ in range(3):
        if s.screen == 0:
            break
        s.click(*BACK, right=True); s.run(GAP)
    ui.settle(max_frames=600)
    st = l2state.snapshot(s.read)
    c = next((x for x in st["counties"] if x["id"] == st["selected_county"]), {})
    return "started " + c.get("castle", {}).get("building", "?") if c.get("castle", {}).get("building") else "not started"
