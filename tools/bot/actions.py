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
MERC_SLIDER_MORE, MERC_CONTINUE, MERC_HIRE = (305, 170), (490, 362), (370, 290)
DRAFTED = 0x543FD8                         # the raise screen's soldier count (FUN_00418653 draws it at x 325) [live]
RATION_UP, RATION_DOWN = (356, 141), (380, 141)
BACK = (300, 240)


def _dismiss_hint(s):
    if s.screen == 0x27:
        s.click(*BACK, right=True)
        s.run(GAP)


def _mine(s):
    st = l2state.snapshot(s.read)
    return st, [a for a in st["armies"] if a["owner"] == st["me"] and a.get("kind", "army") == "army"]


def raise_army(s, ui, men, hire=False):
    """draft about `men` peasants in the selected county (slider ~4-5 men a click), and hire the mercenary band
    offered there when `hire` → the new army or None. With an offer the panel grows a mercenary box and the
    slider / Continue move (slider (305,170), thumbs (370,290) / (415,290), Continue (490,362)) [live: 100
    Scottish Pikemen hired for 1800 crowns, turn 6]."""
    st0, before = _mine(s)
    sel = st0["selected_county"]
    offer = next((c.get("merc_offer") for c in st0["counties"] if c["id"] == sel), 0)
    s.click(*RAISE_ICON); s.run(30)
    _dismiss_hint(s)
    if s.screen != 0x17:
        ui.settle()
        return None
    slider, cont = (MERC_SLIDER_MORE, MERC_CONTINUE) if offer else (SLIDER_MORE, CONTINUE)
    for _ in range(120 if men > 0 else 0):     # the step varies (~1-4 % of the county a click): read it back
        if s.i32(DRAFTED) >= men:
            break
        s.click(*slider, frames=2)
    if offer and hire:
        s.click(*MERC_HIRE); s.run(10)
    if men <= 0 and not (offer and hire):
        s.click(*BACK, right=True); s.run(GAP)
        ui.settle()
        return None
    s.click(*cont); s.run(30)
    _dismiss_hint(s)
    s.click(*CREATE); s.run(40)
    ui.settle()
    st, after = _mine(s)
    new = [a for a in after if a["id"] not in {b["id"] for b in before}]
    return new[0] if new else None


def select_army(s, army, st, ui=None):
    """make `army` visible and click it → True when retail entered move mode (screen 0x10). An event scroll that
    arrives meanwhile (an AI's reply to a gift lands a few seconds after it) eats the click: answer it, retry."""
    for attempt in range(2):
        if ui is not None and s.screen == 0 and ui.modal():
            ui.popup()
        if _select_army(s, army, st):
            return True
        if ui is None or not ui.modal():
            return False
    return False


def _select_army(s, army, st):
    home = next((c for c in st["counties"] if c["id"] == army["county"]), None)
    pt = mapview.locate(s, *army["pos"])
    if pt is None and home is not None:
        s.click(*home["minimap_click"]); s.run(GAP)
        pt = mapview.locate(s, *army["pos"])
    if pt is None and mapview.scroll_to(s, *army["pos"]):
        pt = mapview.locate(s, *army["pos"])
    if pt is None:
        return False
    for _ in range(3):                     # a click right after an event scroll was dismissed over the map is
        s.click(*pt); s.run(GAP)           # swallowed (FUN_0047685d sets DAT_0052afa4) [live: after a gift reply]
        if s.screen == 0x27:               # the one-time "Army Movement" hint
            _dismiss_hint(s)
            continue
        if s.screen == 0x10:
            return True
        if s.screen != 0:
            return False
    return s.screen == 0x10


def march(s, ui, army, target_cell, intent=None):
    """order `army` toward logical cell `target_cell` (a county's town to claim / attack it, an enemy castle to
    besiege it, our castle to garrison it, another of our armies to combine with it). Those destinations pop the
    confirm dialog 0x1e ("Besiege castle?" / "Garrison castle?" / "Combine armies?"): answered yes when it matches
    `intent` ("besiege" / "garrison" / "combine"), else no."""
    st = l2state.snapshot(s.read)
    if not select_army(s, army, st, ui):
        return "cannot select"
    if not mapview.scroll_to(s, *target_cell):
        s.click(*BACK, right=True); s.run(GAP)
        return "target not reachable on screen"
    pt = mapview.locate(s, *target_cell)
    s.move(*pt, frames=DWELL)
    s.click(*pt); s.run(GAP)
    confirmed = None
    if s.screen == 0x1e:
        kind = s.i32(l2state.CONFIRM_KIND)
        confirmed = ui.confirm(want=(intent == "besiege" and kind == 8) or (intent == "garrison" and kind == 7)
                               or (intent == "combine" and kind == 5))
        if not confirmed["answer"]:
            ui.settle(max_frames=600)
            return f"declined {confirmed['text']}"
    if s.screen == 0x10:                   # still selected: the destination was refused (no path?)
        s.click(*BACK, right=True); s.run(GAP)
        return "refused"
    if s.screen == 0x1d or confirmed:      # arrived at once: the siege board / the garrison
        ui.settle(max_frames=1200)
        return intent or "confirmed"
    st2, mine = _mine(s)
    a2 = next((x for x in mine if x["id"] == army["id"]), None)
    if a2 is not None and list(a2["dest"]) == list(target_cell):
        return "ordered"
    return f"screen {s.screen:#x}"


# ---- garrison: an army in a castle leaves it (RMB the castle → "View these troops?" → the first button of
# the army panel FUN_0041b19d: list 0x4dc5a8, handler FUN_004374c4 → engine_combat_transfer_army) [live]
VIEW_TROOPS, PANEL_ROW = (355, 425), 0x553D2C


def leave_castle(s, ui, army, castle_cell, button=68):
    """the garrison marches out (the troop panel's first button); button=132: disband it instead"""
    ui.settle(max_frames=200)
    pt = mapview.locate(s, *castle_cell)
    if pt is None and mapview.scroll_to(s, *castle_cell):
        pt = mapview.locate(s, *castle_cell)
    if pt is None:
        return "castle not on screen"
    s.move(*pt, frames=6); s.click(*pt, right=True); s.run(30)
    if s.screen != 0x04:
        ui.settle(max_frames=600)
        return f"no castle panel ({s.screen:#x})"
    s.move(*VIEW_TROOPS, frames=6); s.click(*VIEW_TROOPS); s.run(30)
    y = 372 + 16 * s.i32(PANEL_ROW)
    if s.screen != 0x04 or not 300 < y < 470:
        ui.settle(max_frames=600)
        return "no troop panel"
    s.move(button, y, frames=6); s.click(button, y); s.run(40)
    if button == DISBAND and s.screen == 0x1e:   # "Disband army?" (confirm kind 6)
        ui.confirm(want=True)
    ui.settle(max_frames=600)
    st, mine = _mine(s)
    a2 = next((x for x in mine if x["id"] == army["id"]), None)
    if button == DISBAND:
        return "disbanded" if a2 is None else "still there"
    return "left" if a2 and not a2.get("garrisoned") else "still inside"


# the troop panel's second button (table 0x4dc5a8 entry 1, FUN_0043733a) → confirm 6 → FUN_0043744f →
# FUN_00438681: the army's men go back to the county [live 2026-10-24: bot3-s103-t36's garrison]
DISBAND = 132


def disband(s, ui, army, castle_cell):
    return leave_castle(s, ui, army, castle_cell, button=DISBAND)


# ---- Options > Advanced > "Fight humans only?" (DAT_0053f284: 1 = No, the default; 0 = Yes → battles against
# the AI skip the "take the field?" prompt and play the quick animation 0x13, FUN_00422c12) [live]
OPTIONS_MENU, OPTIONS_ADVANCED, FIGHT_HUMANS_ONLY = (115, 10), (140, 40), (292, 262)
QUICK_BATTLES = 0x53F284


def quick_battles(s, ui, on=True):
    if (s.i32(QUICK_BATTLES) == 0) == on:
        return on
    s.click(*OPTIONS_MENU); s.run(30)
    s.click(*OPTIONS_ADVANCED); s.run(40)
    _dismiss_hint(s)                       # the first visit: "Advanced Game Options:" help scroll
    if s.screen == 0x39:
        s.click(*FIGHT_HUMANS_ONLY); s.run(30)
    s.click(*BACK, right=True); s.run(30)
    ui.settle(max_frames=600)
    return s.i32(QUICK_BATTLES) == 0


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


# ---- diplomacy (screen 0x0b): the other lords' portraits down the left in realm order (x 90, y 90 + 100 k; the first
# is selected on opening), the selected lord's actions on the right: gift (415,118) → +10 crowns a click (200,255) →
# dispatch (303,293); compliment (415,168); insult (415,218); for an ally also terminate the alliance (415,268), ask
# for help (415,318) and ask to attack (415,368) → screen 0x1a "Plead for help from …": pick the county on its map
# (the minimap grid at (96,176): a county's minimap pixel - (384,-151)), then the thumbs up (335,262)
# [live 2026-10-24: bot2cen-s72-t30 turn 11, "Help in Lancashire" dispatched, the Countess's reply next turn]
DIPLOMACY_ICON, NOBLE, GIFT_MORE, DISPATCH = (622, 447), (415, 118), (200, 255), (303, 293)
DIPLO_ACTION = {"gift": (415, 118), "compliment": (415, 168), "insult": (415, 218), "terminate": (415, 268),
                "help": (415, 318), "attack": (415, 368)}
PLEA_YES = (335, 262)
MESSAGE_SEND = (302, 308)                  # compliment / insult / terminate: the dispatch thumbs


def _portrait(st, realm):
    others = [r["id"] for r in st["realms"] if r["id"] != st["me"] and r.get("active")]
    return (90, 90 + 100 * others.index(realm)) if realm in others else None


def diplomacy(s, ui, action, realm=None, crowns=0, county=None):
    """one diplomatic act toward `realm` (default: the first lord listed): gift `crowns`, compliment, insult, or for
    an ally ask for help in / ask to attack `county` → what happened"""
    st = l2state.snapshot(s.read)
    s.click(*DIPLOMACY_ICON); s.run(30)
    _dismiss_hint(s)
    if s.screen != 0x0b:
        ui.settle(max_frames=600)
        return f"diplomacy did not open ({s.screen:#x})"
    if realm is not None:
        pt = _portrait(st, realm)
        if pt is None:
            s.click(*BACK, right=True); s.run(GAP); ui.settle(max_frames=600)
            return "no such lord"
        s.click(*pt); s.run(20)
    s.click(*DIPLO_ACTION[action]); s.run(30)
    _dismiss_hint(s)
    result = "sent"
    if action == "gift":
        for _ in range(max(1, min(50, crowns // 10))):
            s.click(*GIFT_MORE, frames=3)
        s.run(10)
        s.click(*DISPATCH); s.run(30)
    elif action in ("compliment", "insult", "terminate"):
        # the message panel's own thumbs (list 0x4dda30: 288..317 x 292..324); without it the RMB below cancels
        # the message [live 2026-11-04: a compliment's attitude +15 lands only after this click]
        s.move(*MESSAGE_SEND, frames=4); s.click(*MESSAGE_SEND); s.run(30)
    elif action in ("help", "attack"):
        c = next((x for x in st["counties"] if x["id"] == county), None)
        if s.screen != 0x1a or c is None or not c.get("minimap_click"):
            result = f"no plea screen ({s.screen:#x})"
        else:
            mx, my = c["minimap_click"]
            s.click(mx - 384, my + 151); s.run(20)
            s.move(*PLEA_YES, frames=4); s.click(*PLEA_YES); s.run(30)
    for _ in range(3):
        if s.screen == 0:
            break
        s.click(*BACK, right=True); s.run(GAP)
    s.run(200)                             # a reply scroll arrives a few seconds later
    ui.settle(max_frames=600)
    return result


def gift(s, ui, crowns, realm=None):
    return diplomacy(s, ui, "gift", realm, crowns=crowns)


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


# ---- a custom game (title → Single player → Custom game → title/shield page → the setup page 7 → Start) with
# the defaults (five nobles, easy, 1000 crowns) on a chosen map [live 2026-10-23: England 14 counties,
# Scotland 10, France 16; each lord starts with one county]
CUSTOM_MAPS = {"England": 148, "Scotland": 163, "France": 196}     # y of the map list entry (x 548)
SINGLE_PLAYER, CUSTOM_GAME, TITLE_CONTINUE, CUSTOM_START = (320, 107), (320, 211), (432, 227), (275, 204)


def custom_game(s, ui, map_name="England"):
    s.run(max(0, 600 - (s.frame or 0)))    # the title menu
    s.click(*SINGLE_PLAYER); s.run(100)
    s.click(*CUSTOM_GAME); s.run(150)
    s.click(*TITLE_CONTINUE); s.run(150)
    if map_name != "England":
        s.click(548, CUSTOM_MAPS[map_name]); s.run(40)
    s.click(*CUSTOM_START); s.run(300)
    ui.settle()
    return l2state.snapshot(s.read)["map"]


# ---- county upkeep for long unattended runs (rd seasons --feed) ----
MEAT_TRACK_X0 = 224                        # rations panel: a click on the track at x sets meat_pct = x - 224 (0..100)
MEAT_TRACK_Y = 231                         # (engine_county_ration_slider_render 0x411fde: track 224..323 at y 230..233) [live]
RATION_SHARE = {1: 0.25, 2: 0.5, 3: 1.0}  # food a head per season: quarter, half, normal (tools/bot/policy.py)
DAIRY_PER_COW = 5
FED_PER_SACK = 6                           # a grain sack feeds 6 (rations panel: Fed 168 / Eaten 28 grain at 653 people, quarter) [live]


def feed_plan(c):
    """→ (ration level 1..3, meat_pct) that a county's stores carry: grain first (half the store a season, 6 fed a
    sack; the rest stays for the seasons to the harvest — siegeknights4's quarter rations from counting sacks as heads
    took health to 0 with grain in store), dairy (5 a cow) free, meat — slaughtered cattle — only for a shortfall
    and never below 20 head (the herd breeds; campaign 6's county ate it at meat 100 % with grain unused)"""
    pop = max(1, c.get("population") or 0)
    food = c.get("food") or {}
    grain = max(0, food.get("grain") or 0) * FED_PER_SACK // 2     # heads half the store feeds this season
    cattle = max(0, food.get("cattle") or 0)
    dairy = DAIRY_PER_COW * cattle
    meat_ok = max(0, cattle - 20) * 4       # a slaughtered cow feeds ~4 (unmeasured; a ceiling on the slider below)
    budget = grain + dairy
    level = 1
    for r in (2, 3):
        if pop * RATION_SHARE[r] <= budget:
            level = r
    need = max(0, pop * RATION_SHARE[level] - dairy)
    from_meat = 0 if need <= grain else min(meat_ok, need - grain)
    pct = 0 if need <= 0 else min(100, round(100 * from_meat / need))
    return level, pct


def set_meat(s, ui, pct, read_pct):
    """the rations panel's grain .. meat slider: one click on its track"""
    ui._panel((600, 218), 0x19)
    s.click(MEAT_TRACK_X0 + max(0, min(100, pct)), MEAT_TRACK_Y, frames=4)
    s.run(6)
    ui.close()
    return read_pct()


def upkeep(s, ui, log=print, tax_floor_happy=35, buy=True):
    """keep every county of ours fed and calm before an End turn: rations / meat slider by feed_plan, tax to 0 when
    happiness drops under `tax_floor_happy` (rebels at < 25), back to 5 % when crowns run under 200 and the county is
    content (> 60); first, grain from a merchant in the county (up to 3 seasons at normal rations, 60 a trade, ~3
    crowns a sack) when the store feeds under two seasons and crowns are over 300 (nothing is sown: a long run's
    harvest stops). → [(county, what changed)]"""
    st = l2state.snapshot(s.read)
    me, out = st["me"], []
    CB, CS = l2state.COUNTY_BASE, l2state.COUNTY_STRIDE
    crowns = next((r.get("crowns", 0) for r in st["realms"] if r["id"] == me), 0)
    for c in st["counties"]:
        if c["owner"] != me or "food" not in c:
            continue
        cid = c["id"]
        if s.u32(l2state.SEL_COUNTY) != cid:
            if not c.get("minimap_click"):
                continue
            s.click(*c["minimap_click"]); s.run(GAP)
            ui.settle(max_frames=600)
            if s.u32(l2state.SEL_COUNTY) != cid:
                out.append((cid, "not selected"))
                continue
        rd = lambda o: s.read(CB + cid * CS + o, 1)[0]
        rd4 = lambda o: s.i32(CB + cid * CS + o)
        did = {}
        sacks = c["food"].get("grain") or 0
        if buy and sacks * FED_PER_SACK < 2 * c.get("population", 0) and crowns > 300 and any(
                m["county"] == cid for m in st.get("merchants", [])):
            want = (3 * c.get("population", 0) + FED_PER_SACK - 1) // FED_PER_SACK - sacks   # 3 seasons, normal
            res = []
            while want > 0 and len(res) < 8:
                before = rd4(0x224)
                res.append(trade(s, ui, "grain", min(60, want)))
                if rd4(0x224) <= before:
                    break
                want -= rd4(0x224) - before
            did["bought_grain"] = (res[-1], rd4(0x224) - sacks)
            crowns = s.i32(l2state.REALM_BASE + me * l2state.REALM_STRIDE + 0x118)
        c["food"]["grain"] = rd4(0x224)           # after any purchase
        level, pct = feed_plan(c)
        if rd(0x15E) != level:
            did["ration"] = set_rations(s, ui, level, lambda: rd(0x15E))
        cur = rd(0x15F)
        if abs(cur - pct) > 3:
            did["meat_pct"] = set_meat(s, ui, pct, lambda: rd(0x15F))
        happy, tax = c.get("happiness", 50), rd(0xB9)
        want = 0 if happy < tax_floor_happy else (5 if crowns < 200 and happy > 60 and tax < 5 else tax)
        if want != tax:
            did["tax"] = ui.set_tax(want, lambda: rd(0xB9))
        if did:
            out.append((cid, did))
    for cid, d in out:
        log(f"  upkeep: county {cid} {d}")
    return out
