#!/usr/bin/env python3
"""l2state.py — read the Lords of the Realm II strategic game state out of retail memory.

    snapshot(read) -> dict          read(va, n) -> bytes   (e.g. rhbot.Session.read)

One call reads ~120 KB in ~25 `read`s (the realm / county / army record blocks, the 64x64 map-cell
grid, a few scalars, the localization blob for names) and returns a JSON-serializable, decision-
oriented dict. Every field's VA / record offset + the evidence for it is in
docs/findings/game-state-map.md; the comments below give the offset and how it was verified:
  [live]  confirmed in rhost against the UI (screenshot) or by a UI perturbation (tax/ration/labor/
          army/castle/end-turn) moving exactly that byte
  [port]  read by a byte-exact (render-verified vs retail) port module in src/, not re-checked live
  [decomp] read off the Ghidra decompile of the named retail function, not exercised live
  [notes] from docs/findings / older Frida tools only

CLI (boots a scenario in rhost under wine, runs to the end of its trace, prints the snapshot):
    nix develop -c python3 tools/bot/l2state.py newgame-playnow [--frame N] [--fair PLAYER] [--compact]

Record blocks (retail ImageBase 0x400000; all three are save-schema entries, so they are the save):
  realm   0x0057bf00 + p*0x160  p=1..5 (0 unused)   g_player_strategic_352
  county  0x0053f9b0 + k*0x300  k=1..n (n = 0x0056d5dc)  g_per_army_768 (sic — it is the COUNTY record)
  piece   0x0052f0b0 + i*0x1a4  i=1..150             g_per_territory_420 (sic — armies, merchants,
                                                     rebels, supply carts: every map piece)
"""
import json
import os
import struct
import sys

# ---------------------------------------------------------------------------------------------
# addresses
REALM_BASE, REALM_STRIDE, N_REALMS = 0x57BF00, 0x160, 5
COUNTY_BASE, COUNTY_STRIDE, MAX_COUNTIES = 0x53F9B0, 0x300, 16       # 17 slots, slot 0 unused
PIECE_BASE, PIECE_STRIDE, N_PIECES = 0x52F0B0, 0x1A4, 150            # 151 slots, slot 0 unused
REALM_NAMES = 0x553D50          # 6 x 0x2c; C-string name at +4 (16 bytes) [live: "The Knight"]
CELLS = 0x522F90                # 64x64 map cells x 8 B, cell = (row*64+col)*8 [live]
FIELD_LISTS = 0x53EA00          # county k: 20 x i32 cell byte-offsets at +k*0x50 (FUN_00469b8d) [live]
PRICES = 0x5532A0               # 16 goods x 0x10, +0 i32 base price (save entry 82) [port: engine_trade]
LOC_BLOB = 0x591580             # L2.eng as loaded ("Textfile"), 24-bit offset table at +8 [live]
MINIMAP = 0x52AFF0              # 128x128 county-id grid; pixel (x,y) = screen (480+x, 25+y) [live click]

# scalars (save-schema slots unless noted)
SCREEN = 0x4EAC50               # u8 g_sim_current_screen [live]
ENGINE_STATE = 0x52AF94         # i32 engine state (3 = in game) [live, rhost status]
GAME_MODE = 0x57C8FC            # 0 strategic / 2 tactical combat (save-format.md, FUN_00440231) [notes]
MODAL = 0x53E910                # event scroll up ("extended pause"; RMB does not dismiss) [notes]
SEASON = 0x57C934               # 1 Spring .. 4 Winter [live]
YEAR = 0x553EDC                 # AD; +1 entering Winter [live: Winter 1268 -> Spring/Summer 1268]
TURN = 0x553240                 # turn counter, 1 = first season [live: 1 -> 3 over endturn-2]
LOCAL_PLAYER = 0x57C8CC         # realm whose HUD/panels are shown (the human) [live]
SEL_COUNTY = 0x57C93C           # selected county (HUD) [live: minimap clicks]
N_COUNTIES = 0x56D5DC           # counties on this map [live: 4 on Play Now]
MAP_ID = 0x53F034               # county-name page: loc group 100 sub map_id*20 + k [live]
DIFFICULTY = 0x53F23C           # 0.. (tools/cheat_data.py upkeep-divisor table) [notes]
NOT_READY = 0x554004            # players still mid-turn (FUN_0049b762) [live]
CAMERA = 0x5651B4               # i32 col, +4 row: strategic scroll origin (tools/ai_pov.py) [notes]
SEL_PIECE = 0x52AFBC            # piece index under the last map query / selection [notes]

SEASONS = {1: "Spring", 2: "Summer", 3: "Autumn", 4: "Winter"}
SCREENS = {0x00: "map", 0x02: "workforce", 0x04: "terrain_info", 0x08: "merchant", 0x09: "court",
           0x0a: "armory", 0x0b: "diplomacy", 0x0c: "trade", 0x0d: "arm_troops", 0x0f: "labor",
           0x10: "army_move", 0x11: "split_army", 0x12: "battle", 0x13: "battle", 0x14: "population",
           0x15: "tax", 0x16: "happiness", 0x17: "raise_army", 0x18: "send_supplies", 0x19: "rations",
           0x1a: "diplomacy_action", 0x1b: "castle", 0x1c: "victory_defeat", 0x1d: "siege",
           0x1e: "confirm", 0x20: "rankings", 0x22: "movie", 0x23: "turn_processing", 0x27: "scroll"}
RATIONS = ["none", "quarter", "half", "normal", "double", "triple"]               # loc grp 0x15
HEALTH = ["diseased", "sick", "average", "good", "perfect"]                      # loc grp 0x14
# weapon index w: realm stock +0x140+4w, smithy selector (county +0x290), troop class = w+1
WEAPONS = ["crossbows", "maces", "swords", "pikes", "bows", "armour"]
# troop class c (piece +0x16c + 2c): loc grp 8 sub 0x34+2c; weights = combat strength (DAT_004d4b98)
TROOPS = ["peasants", "crossbowmen", "macemen", "swordsmen", "pikemen", "archers", "knights"]
TROOP_WEIGHT = [2, 16, 8, 13, 9, 13, 22]
GOODS = [None, "grain", "cattle", "sheep", "ale", "wool", "iron", "stone", "timber", "pikes",
         "bows", "maces", "crossbows", "swords", "mail"]                         # loc grp 6
CASTLES = ["none", "wooden palisade", "motte and bailey", "norman keep", "stone castle",
           "royal castle"]                                                       # loc grp 0x47
MERCS = [None, "Scottish", "Irish", "Moorish", "Welsh", "Danish", "Swedish", "Flemish", "Norman",
         "Saxon", "Burgundian", "Spanish", "Angevin"]                            # loc grp 0x10
PIECE_KIND = {1: "army", 2: "rebels", 3: "merchant", 4: "supplies"}             # piece +0x08
TASKS = ["grain", "cattle", "reclaim", "castle", "iron", "stone", "wood", "smithy", "idle"]
# industry lanes: county +0x294 + lane*0x18 (lane order wood, iron, smithy, stone — FUN_00412e6b)
LANES = {"wood": 0, "iron": 1, "smithy": 2, "stone": 3}
SEA_REGION = 0x20


# ---------------------------------------------------------------------------------------------
def _i32(b, o):
    return struct.unpack_from("<i", b, o)[0]


def _i16(b, o):
    return struct.unpack_from("<h", b, o)[0]


def _s8(b, o):
    return struct.unpack_from("<b", b, o)[0]


class _Mem:
    def __init__(self, read):
        self._read = read

    def bytes(self, va, n):
        out = b""
        while n > 0:
            k = min(n, 0x10000)
            out += self._read(va, k)
            va, n = va + k, n - k
        return out

    def u8(self, va):
        return self.bytes(va, 1)[0]

    def i32(self, va):
        return _i32(self.bytes(va, 4), 0)


def _cstr(b):
    return b.split(b"\0", 1)[0].decode("latin-1").strip()


def _loc_group(mem, group, first, count):
    """strings `first`..`first+count-1` of L2.eng group `group`, read from the loaded blob.
    The localizer's offset table is 24-bit LE at blob+8+group*4 (FUN_004017bf); within a group the
    strings are NUL-separated (no empty strings inside groups, so a split matches the walker)."""
    def off(g):
        b = mem.bytes(LOC_BLOB + 8 + g * 4, 4)
        return b[0] | b[1] << 8 | b[2] << 16
    a, z = off(group), off(group + 1)
    if not 0 < a < z < 120000 or z - a > 0x8000:
        return [None] * count
    parts = mem.bytes(LOC_BLOB + a, z - a).split(b"\0")
    return [parts[i].decode("latin-1") if i < len(parts) else None for i in range(first, first + count)]


def _field_class(t):
    """map-cell type of a field (cell +0, flags&0x20) -> (category, detail); table DAT_004d2ec8 / grp 0x1e"""
    if t == 0:
        return "barren", None
    if t == 1:
        return "fallow", None
    if 2 <= t <= 14:
        return "wheat", ("unused", "sown", "ripening", "harvest_next", "ready")[0 if t == 2 else 1 + (t - 3) % 4]
    if 15 <= t <= 18 or 25 <= t <= 28:
        return "reclaiming", None
    if 19 <= t <= 22:
        return "cattle", t - 19          # crowding 0 empty, 1 ample, 2 some overcrowding, 3 overcrowded
    if t == 23:
        return "flooded", None
    if t == 24:
        return "parched", None
    return "other", t


# ---------------------------------------------------------------------------------------------
def snapshot(read, fair=None):
    """The strategic state as a dict. `read(va, n) -> bytes` reads retail memory.

    fair=None: everything (a state reader). fair=P: only what realm P's player can see in the UI —
    other realms' treasuries/stocks/relations and foreign counties' economies are omitted."""
    mem = _Mem(read)
    scal = lambda va: mem.i32(va)
    n_counties = max(0, min(MAX_COUNTIES, scal(N_COUNTIES)))
    me = scal(LOCAL_PLAYER)
    map_id = scal(MAP_ID)
    realm_blk = mem.bytes(REALM_BASE, REALM_STRIDE * (N_REALMS + 1))
    county_blk = mem.bytes(COUNTY_BASE, COUNTY_STRIDE * (MAX_COUNTIES + 1))
    piece_blk = mem.bytes(PIECE_BASE, PIECE_STRIDE * (N_PIECES + 1))
    names_blk = mem.bytes(REALM_NAMES, 0x2C * 6)
    cells = mem.bytes(CELLS, 64 * 64 * 8)
    fields_blk = mem.bytes(FIELD_LISTS, 0x50 * (MAX_COUNTIES + 1))
    prices_blk = mem.bytes(PRICES, 0x10 * 16)
    county_names = _loc_group(mem, 100, map_id * 20, MAX_COUNTIES + 1)
    R = lambda p: realm_blk[p * REALM_STRIDE:(p + 1) * REALM_STRIDE]
    C = lambda k: county_blk[k * COUNTY_STRIDE:(k + 1) * COUNTY_STRIDE]
    P = lambda i: piece_blk[i * PIECE_STRIDE:(i + 1) * PIECE_STRIDE]
    region_of = lambda col, row: cells[((row & 63) * 64 + (col & 63)) * 8 + 7]   # cell +7 [live]

    screen = mem.u8(SCREEN)
    me_ready = _i32(R(me), 0) if 1 <= me <= N_REALMS else None
    modal = mem.u8(MODAL)
    st = {
        "screen": screen, "screen_name": SCREENS.get(screen, f"0x{screen:02x}"),
        "engine_state": scal(ENGINE_STATE),                         # 2 boot/title, 3 in game [live]
        "mode": {0: "strategic", 2: "combat"}.get(scal(GAME_MODE), scal(GAME_MODE)),
        "modal_event": bool(modal),
        # strat +0 is the per-realm turn-ready value: <999 still playing, 999 human done, 1000 AI done;
        # after End turn: 999 on the map, then screen 0x23 (ready reset to 0), then 1 = the new turn [live]
        "awaiting_input": bool(screen == 0 and not modal and me_ready is not None and 0 < me_ready < 999),
        "turn_resolving": bool(screen == 0x23 or (me_ready is not None and me_ready >= 999)),
        "date": {"turn": scal(TURN), "season": scal(SEASON), "season_name": SEASONS.get(scal(SEASON)),
                 "year": scal(YEAR)},
        "me": me, "selected_county": scal(SEL_COUNTY), "difficulty": scal(DIFFICULTY),
        "camera": [scal(CAMERA), scal(CAMERA + 4)],
        "map": {"id": map_id, "counties": n_counties},
    }
    # engine state > 2 = in a game (the End-turn strip gate in FUN_0041a734); before that the
    # record blocks hold boot defaults (n_counties reads 14 on the title screen) [live]
    st["in_game"] = st["engine_state"] > 2
    if not st["in_game"]:
        return st

    # ---- pieces (armies, merchants, rebels, supply carts) -------------------------------------
    pieces, army_names = [], {}
    for i in range(1, N_PIECES + 1):
        r = P(i)
        if r[0x00] == 0:                 # +0x00 owner (i8); 0 = dead record (FUN_004650b0) [port]
            continue
        kind = r[0x08]                   # +0x08 kind 1 army / 2 rebels / 3 merchant / 4 supplies [live 1,3; decomp FUN_0041b19d 2,4]
        col, row = r[0x0A], r[0x0B]      # +0x0a/+0x0b cell col/row [live: army move]
        pc = {"id": i, "kind": PIECE_KIND.get(kind, kind), "owner": _s8(r, 0x00),
              "pos": [col, row], "county": region_of(col, row)}
        if kind == 1 and 1 <= pc["owner"] <= N_REALMS:          # +0x14f name: loc grp 0x5d+owner [live: "The Lions."]
            if pc["owner"] not in army_names:
                army_names[pc["owner"]] = _loc_group(mem, 0x5D + pc["owner"], 0, 25)
            nm = army_names[pc["owner"]]
            pc["name"] = nm[r[0x14F]].strip('"') if r[0x14F] < len(nm) and nm[r[0x14F]] else None
        if kind in (1, 2):
            troops = [_i16(r, 0x16C + 2 * c) for c in range(7)]   # +0x16c 7 x i16 [live: info panel]
            merc_n, merc_cls, merc_nat = r[0x196], r[0x195], r[0x197]   # [decomp: FUN_0041b19d / FUN_004224e7]
            tot = list(troops)
            if merc_n and merc_cls < 7:
                tot[merc_cls] += merc_n
            men = _i32(r, 0x168)                                   # +0x168 total men [live]
            pc.update({
                "home": r[0x11],                                   # +0x11 home county [live: "An army from"]
                "men": men,
                "troops": {TROOPS[c]: tot[c] for c in range(7) if tot[c]},
                "strength": sum(tot[c] * TROOP_WEIGHT[c] for c in range(7)),
                "wages": _i32(r, 0x15C),                           # +0x15c seasonal wage [live: 15 Crowns]
                "moves_left": max(0, 15 - _s8(r, 0x153)),          # 15 - +0x153 [live: 15 -> 2 after a march]
                "moving": r[0x14C] != 0,                           # +0x14c movement sub-state [live]
                "dest": [r[0x14], r[0x15]],                        # +0x14/+0x15 march goal cell [live]
                "order": r[0x1A],                                  # +0x1a order code (AI brain step 0xb), 5 = garrison [port]
                "formed": _i16(r, 0x164),                          # +0x164 year formed [live: 1268 AD]
                "health": r[0x155],                                # +0x155 0 healthy .. 4 dying (grp 0x1f sub 0x1b+) [decomp: FUN_0041b19d]
                "besieging": r[0x199],                             # +0x199 siege link, 0 = not besieging [port: engine_army_state]
            })
            if merc_n:
                pc["mercenaries"] = {"count": merc_n, "class": TROOPS[merc_cls] if merc_cls < 7 else merc_cls,
                                     "nation": MERCS[merc_nat] if merc_nat < len(MERCS) else merc_nat}
        elif kind == 3:
            pc["trading_in"] = r[0x167]                            # +0x167 county served [live: = cell county]
            pc["markup_pct"] = r[0x166]                            # +0x166 buy markup %, 100 at spawn [live: buy = 2x]
        elif kind == 4:
            pc["to"] = r[0x167]                                    # +0x167 destination county [decomp: FUN_0041b19d]
            pc["grain"] = _i16(r, 0x16C)                           # +0x16c sacks [decomp: FUN_0041b19d]
            pc["cattle"] = _i16(r, 0x170)                          # +0x170 animals [decomp: FUN_0041b19d]
        pieces.append(pc)

    # ---- adjacency from the cell grid (4-neighbourhood between different county keys) --------
    nbr = {k: set() for k in range(1, n_counties + 1)}
    for row in range(64):
        for col in range(64):
            a = cells[(row * 64 + col) * 8 + 7]
            if not 1 <= a <= n_counties:
                continue
            for dc, dr in ((1, 0), (0, 1)):
                c2, r2 = col + dc, row + dr
                if c2 < 64 and r2 < 64:
                    b = cells[(r2 * 64 + c2) * 8 + 7]
                    if 1 <= b <= n_counties and b != a:
                        nbr[a].add(b)
                        nbr[b].add(a)
    coastal = {cells[i * 8 + 7] for i in range(4096)
               if any(0 <= j < 4096 and cells[j * 8 + 7] == SEA_REGION for j in (i - 1, i + 1, i - 64, i + 64))}

    # ---- minimap click points (the screen pixel that selects each county) --------------------
    mm = mem.bytes(MINIMAP, 128 * 128)
    pts = {}
    for y in range(128):
        rowb = mm[y * 128:(y + 1) * 128]
        for x in range(128):
            v = rowb[x]
            if 1 <= v <= n_counties:
                pts.setdefault(v, []).append((x, y))
    mm_click = {}
    for k, v in pts.items():
        cx, cy = sum(p[0] for p in v) / len(v), sum(p[1] for p in v) / len(v)
        bx, by = min(v, key=lambda p: (p[0] - cx) ** 2 + (p[1] - cy) ** 2)
        mm_click[k] = [0x1E0 + bx, 0x19 + by]

    # ---- counties --------------------------------------------------------------------------------
    counties = []
    owned = {p: [] for p in range(N_REALMS + 2)}
    for k in range(1, n_counties + 1):
        r = C(k)
        owner = r[0x05]                                            # +0x05 owner realm, 0 neutral [live]
        owned.setdefault(owner, []).append(k)
        fl = struct.unpack_from("<20i", fields_blk, k * 0x50)
        fields = {"total": 0, "fallow": 0, "wheat": 0, "cattle": 0, "reclaiming": 0, "barren": 0,
                  "flooded": 0, "parched": 0}
        wheat, crowd = {}, 0
        for o in fl:
            if 0 < o < len(cells):
                cat, det = _field_class(cells[o])
                fields["total"] += 1
                fields[cat] = fields.get(cat, 0) + 1
                if cat == "wheat":
                    wheat[det] = wheat.get(det, 0) + 1
                elif cat == "cattle":
                    crowd = max(crowd, det)
        if wheat:
            fields["wheat_stages"] = wheat
        if fields["cattle"]:
            fields["pasture"] = ["empty", "ample", "crowded", "overcrowded"][crowd]
        here = [p for p in pieces if p["county"] == k]
        defenders = [p for p in here if p["kind"] == "army" and p["owner"] == owner and owner]
        lvl, prev, building = r[0x1C0], r[0x1C1], r[0x1C3]
        castle = {"level": prev if building else lvl}               # +0x1c0 level, +0x1c1 prior while building [live]
        castle["name"] = CASTLES[castle["level"]] if castle["level"] < len(CASTLES) else castle["level"]
        castle["cell"] = [r[0x74], r[0x75]]                        # +0x74/+0x75 castle 2x2 anchor [live: flag-0x80 cells]
        if building:                                               # +0x1c3 1 new / 2 upgrade [live]
            castle.update({"building": CASTLES[lvl] if lvl < len(CASTLES) else lvl,
                           "labor_left": _i32(r, 0x1CC), "labor_total": _i32(r, 0x1D8),   # [live]
                           "stone_needed": _i32(r, 0x1D0), "wood_needed": _i32(r, 0x1D4)})  # [live: wood 350->264]
        c = {
            "id": k, "name": county_names[k] if k < len(county_names) else None, "owner": owner,
            "neighbors": sorted(nbr.get(k, ())), "coastal": k in coastal,
            "capital": [r[0x6C], r[0x6D]],                         # +0x6c/+0x6d county-town cell [live: flag-0x40 2x2]
            "minimap_click": mm_click.get(k),
            "castle": castle,
            "armies_here": [p["id"] for p in here if p["kind"] in ("army", "rebels")],
            "merchant": any(p["kind"] == "merchant" for p in here),
            "fields": fields,
        }
        if fair is None or owner == fair:                          # the HUD shows these for own counties only
            c.update({"population": _i32(r, 0x24),                 # +0x24 [live: 435]
                      "happiness": _s8(r, 0x0C),                   # +0x0c [live: 72]
                      "army_strength": sum(p["strength"] for p in defenders)})
            labor = {}
            for t, name in enumerate(TASKS):                      # +0xc4 + t*0xc {assigned, needed, max} [live]
                o = 0xC4 + t * 0xC
                a, need, mx = _i32(r, o), _i32(r, o + 4), _i32(r, o + 8)
                if a or need > 0 or name == "idle":
                    labor[name] = a if name == "idle" else [a, need, mx]
            ind = {}
            for name, lane in LANES.items():                       # +0x294 + lane*0x18 [live: wood +86]
                o = 0x294 + lane * 0x18
                if r[o + 1] or r[o + 3]:
                    ind[name] = {"operating": bool(r[o + 3]),      # +3 operating (HUD gate) [live]
                                 "efficiency": _s8(r, o),           # +0 % [port: labor panel]
                                 "output": _i32(r, o + 0x14)}       # +0x14 made next season [live]
            if "smithy" in ind:
                w = r[0x290]                                       # +0x290 smithy weapon [port]
                ind["smithy"]["weapon"] = WEAPONS[w] if w < 6 else w
            c.update({
                "population_last": _i32(r, 0x28),                  # +0x28 [live: 417]
                "births": _i32(r, 0x30), "deaths": _i32(r, 0x34),  # [live: +63 / -45]
                "army_change": _i32(r, 0x38),                      # +0x38 (-60 after a 60-man draft) [live]
                "emigrants": _i32(r, 0x3C), "immigrants": _i32(r, 0x40),       # [port]
                "happiness_last": _s8(r, 0x0D), "happiness_avg": _s8(r, 0x18),  # [live: 65 / 72]
                "happiness_from": {"taxes": _s8(r, 0x12), "ration": _s8(r, 0x13), "health": _s8(r, 0x14),
                                   "army": _s8(r, 0x15), "ale": _i32(r, 0x194), "events": _s8(r, 0x17)},  # [live]
                "health": HEALTH[r[0x09]] if r[0x09] < 5 else r[0x09],         # +0x09 [live: Good]
                "tax": r[0xB9], "tax_income": _i32(r, 0xC0),       # +0xb9 %, +0xc0 crowns [live: 3% -> 62]
                "ration": {"wanted": RATIONS[r[0x15E]] if r[0x15E] < 6 else r[0x15E],    # [live]
                           "achieved": RATIONS[r[0x15D]] if r[0x15D] < 6 else r[0x15D],  # [live]
                           "meat_pct": _s8(r, 0x15F)},             # slider 0 grain .. 100 meat [live]
                "food": {"grain": _i32(r, 0x224), "grain_change": _i32(r, 0x22C),        # [live HUD / port]
                         "cattle": _i32(r, 0x250), "cattle_change": _i32(r, 0x258),      # [live: 101, +10]
                         "fed": {"grain": _i32(r, 0x170), "meat": _i32(r, 0x174), "dairy": _i32(r, 0x16C)},
                         "eaten": {"grain": _i32(r, 0x178), "cattle": _i32(r, 0x17C)}},  # [live: rations panel]
                "industry_pct": _s8(r, 0x08),                      # +0x08 HUD farm<->industry split [live]
                "labor": labor,
                "industry": ind,
            })
        counties.append(c)

    # ---- realms --------------------------------------------------------------------------------
    realms = []
    cty_by_id = {c["id"]: c for c in counties}
    for p in range(1, N_REALMS + 1):
        r = R(p)
        if r[0x04] == 0 and not owned.get(p):                      # +0x04 in-game flag [live]
            continue
        human = bool(r[0x05])                                      # +0x05 human-controlled [live]
        mine_c = [cty_by_id[k] for k in owned.get(p, []) if k in cty_by_id]
        mine_a = [a for a in pieces if a["kind"] == "army" and a["owner"] == p]
        rm = {"id": p, "name": _cstr(names_blk[p * 0x2C + 4:p * 0x2C + 0x14]),
              "human": human, "active": bool(r[0x04]),
              "color": r[0x0A],                                    # +0x0a banner colour 1..5 [port]
              "counties": owned.get(p, []),
              "castles": sum(1 for c in mine_c if c["castle"]["level"]),
              "armies": len(mine_a),
              # rank/score are retail's standings, recomputed during turn processing (FUN_0049aa0e);
              # the realm aggregates +0x10 pop / +0x0c happiness / +0x54 troops are too, so the live
              # sums below come from the county / piece records instead.
              "rank": r[0x2B],                                     # +0x2b rank [port: rankings]
              }
        if fair is None or p == fair:
            rm.update({"population": sum(c["population"] for c in mine_c),
                       "troops": sum(a["men"] for a in mine_a)})
        if not human:
            rm["personality"] = r[0x07]                            # +0x07 1..4 (AI only) [notes+port]
        if fair is None or p == fair:
            rm.update({
                "ready": _i32(r, 0x00),                            # +0 turn-ready value [live]
                "crowns": _i32(r, 0x118), "crowns_last": _i32(r, 0x11C),   # +0x118 [live: 5000]
                "tax_income": _i32(r, 0x15C), "army_wages": _i32(r, 0xFC),  # [live: court 0 / 15 after draft]
                "iron": _i32(r, 0x120), "stone": _i32(r, 0x128), "wood": _i32(r, 0x130),  # [live: court]
                "weapons": {WEAPONS[w]: _i32(r, 0x140 + 4 * w) for w in range(6)},       # [live: court]
                "score": _i32(r, 0x50),                            # +0x50 composite score [port]
                "ally": r[0x81],                                   # +0x81 alliance partner (D_LORD) [port: form_alliance]
            })
            rel = {}
            for q in range(1, N_REALMS + 1):                       # per-pair row +q*0x10 (engine_diplomacy.c)
                if q == p or not R(q)[0x04]:
                    continue
                o = q * 0x10
                rel[q] = {"attitude": _s8(r, 0x84 + o),            # +0x84 -30..30, p toward q [live: gift +10]
                          "allied": r[0x85 + o],                   # +0x85 set by form_alliance [port]
                          "at_war": r[0x88 + o],                   # +0x88 war-declared [port]
                          "message_sent": r[0x90 + o]}             # +0x90 q messaged p this turn [live: gift]
            rm["relations"] = rel
        realms.append(rm)
    st["realms"] = realms
    st["counties"] = counties
    # no strategic fog (every piece is drawn); a foreign army's make-up is not shown (info panel
    # FUN_0041b19d draws troops/wages only when owner == the local player)
    public = ("id", "kind", "owner", "name", "pos", "county", "home", "moving", "dest", "besieging")
    st["armies"] = [p if fair is None or p["owner"] == fair else {k: p[k] for k in public if k in p}
                    for p in pieces if p["kind"] in ("army", "rebels")]
    st["merchants"] = [p for p in pieces if p["kind"] == "merchant"]
    st["supplies"] = [p for p in pieces if p["kind"] == "supplies"]
    # trade prices (FUN_00428daf): sell = the base price table; buy = base + max(1, base*markup/100)
    # (ale: no markup) with the serving merchant's markup [live: grain 2/4, cattle 12/24, swords 23/46]
    here = [m for m in st["merchants"] if m["county"] == st["selected_county"]] or st["merchants"]
    mk = here[0]["markup_pct"] if here else 100
    st["prices"] = {}
    for g in range(1, 15):
        base = _i32(prices_blk, g * 0x10)
        if base:
            st["prices"][GOODS[g]] = {"sell": base, "buy": base + (0 if g == 4 else max(1, base * mk // 100))}
    return st


# ---------------------------------------------------------------------------------------------
def main():
    import argparse
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", ".."))
    sys.path.insert(0, os.path.join(root, "tools", "rhost"))
    import rhost
    from rhbot import Session
    ap = argparse.ArgumentParser(description="boot a scenario in rhost (wine), run it, print the strategic state")
    ap.add_argument("scenario", help="tests/scenarios/<name> or a trace.jsonl path")
    ap.add_argument("--frame", type=int, help="stop at this frame (default: the trace's meta frames)")
    ap.add_argument("--fair", type=int, help="only what realm N's player can see")
    ap.add_argument("--compact", action="store_true", help="one-line JSON")
    a = ap.parse_args()
    trace = a.scenario if a.scenario.endswith(".jsonl") else os.path.join(root, "tests", "scenarios", a.scenario, "trace.jsonl")
    if not os.path.exists(trace):
        raise SystemExit(f"no trace at {trace}")
    meta = rhost.trace_meta(trace)
    frame = a.frame or int(meta.get("frames") or 0)
    if not frame:
        raise SystemExit("trace has no meta.frames; pass --frame N")
    s = Session("l2state", target="wine", trace=trace)
    try:
        s.run(frame - (s.frame or 0))
        st = snapshot(s.read, fair=a.fair)
        st["frame"] = s.frame
    finally:
        s.quit()
    print(json.dumps(st, separators=(",", ":")) if a.compact else json.dumps(st, indent=1))


if __name__ == "__main__":
    main()
