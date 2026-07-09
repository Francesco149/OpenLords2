#!/usr/bin/env python3
"""Render the running retail game FROM AN AI PLAYER'S POINT OF VIEW.

Drives a running probe daemon (tools/probe_daemon.py) to flip the strategic
map + HUD to any realm's perspective, for the "LoTR II AI cheats" video: you
watch the enemy AI's economy from the inside — their capital centred, their
county shown as OWNED, the HUD sourcing their (cheated) treasury.

Why this is just a few pokes, not "fog hooks": LoTR2 has **no per-player
strategic fog** (render_mode is a fixed new-game config; the whole map is
visible to everyone) and **ownership is absolute** (each cell stores its true
owning realm, so an AI's counties already render in their colour).  So an AI
POV is only:  camera → their land · viewer global → their HUD · current
territory → their county.  All three are plain writes; centring uses the
game's OWN minimap-click recentre transform (robust across the isometric
projection).

    tools/ai_pov.py pov 2 --shot /tmp/ai2.png   # POV of realm slot 2, capture
    tools/ai_pov.py human                        # restore the human POV
    tools/ai_pov.py data                         # per-realm economy table
    tools/ai_pov.py scan                          # rebuild the region-grid cache

Slots are 1..4 (1 = the human by default; whoever has the human flag +5==1).
State is left switched after `pov` unless --restore; `human` always restores.
"""
from __future__ import annotations

import argparse
import json
import socket
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CTRL = ROOT / "runs" / "probe" / "daemon.json"
CACHE = ROOT / "runs" / "probe" / "region_grid.json"

# ─── game memory layout (grounded live 2026-07-09) ───────────────────────────
VIEWER    = 0x0057c8cc   # DAT_0057c8cc — "local player" the HUD/panel read (i32)
CURTERR   = 0x0057c93c   # s_slot_0057c93c — current/selected territory (i32)
CAM_COL   = 0x005651b4   # DAT_005651b4 — strat scroll column (i32)
CAM_ROW   = 0x005651b8   # DAT_005651b8 — strat scroll row (i32)
REDRAW    = 0x0057d340   # g_demand_redraw — write 2 to force a full strat re-render
DIFFICULTY= 0x0053f23c   # s_slot_0053f23c — upkeep-divisor difficulty (tab 4)

STRAT_BASE, STRAT_STRIDE = 0x0057bf00, 0x160   # g_player_strategic_352
S_ACTIVE, S_HUMAN, S_PERS, S_RANK = 0x04, 0x05, 0x07, 0x2b
S_WAGE_SUM, S_CASH = 0xfc, 0x118

REC_BASE, REC_STRIDE = 0x0053f9b0, 0x300       # g_per_army_768
R_OWNER = 0x05

MMGRID = 0x0052aff0      # DAT_0052aff0 — 128×128 region-id grid (minimap)
MM_W = MM_H = 128
MM_SCREEN_X0, MM_SCREEN_Y0 = 478, 28           # minimap plot origin, 1:1 128×128

DIVISOR_BY_DIFF = {0: 3, 1: 5, 2: 10}          # AI wage = value / this; default 10
HUMAN_DIVISOR = 4                              # human ALWAYS value / 4


# ─── daemon client ───────────────────────────────────────────────────────────
def _port() -> int:
    return int(json.loads(CTRL.read_text())["port"])


def send(req: dict) -> dict:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(45.0)
    s.connect(("127.0.0.1", _port()))
    s.sendall((json.dumps(req) + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = s.recv(65536)
        if not chunk:
            break
        buf += chunk
    s.close()
    return json.loads(buf.decode()) if buf else {"ok": False, "err": "no reply"}


def read(va: int, t: str = "i32"):
    return send({"cmd": "read", "va": va, "type": t}).get("val")


def reads(specs):
    r = send({"cmd": "reads",
              "specs": [{"name": n, "va": v, "type": t} for n, v, t in specs]})
    return r.get("vals", {})


def poke_i32(va: int, val: int):
    return send({"cmd": "poke", "va": va,
                 "bytes": [val & 0xFF, (val >> 8) & 0xFF,
                           (val >> 16) & 0xFF, (val >> 24) & 0xFF]})


def click(x: int, y: int, button: str = "l"):
    return send({"cmd": "click", "x": x, "y": y, "button": button})


def move(x: int, y: int):
    return send({"cmd": "move", "x": x, "y": y})


def sleep_ms(ms: float):
    return send({"cmd": "sleep", "ms": ms})


def shot(path: str | None):
    return send({"cmd": "shot", "path": path})


def force_present_shot(path: str | None, tries: int = 4):
    """Force a full strat re-render and grab a frame.  The map only presents
    when dirty, so we poke g_demand_redraw and nudge the cursor; retry until a
    frame is produced (the present seat can miss the first tick)."""
    for i in range(tries):
        poke_i32(REDRAW, 2)
        sleep_ms(300)
        move(300 + (i % 2) * 30, 240)
        sleep_ms(200)
        r = shot(path)
        if r.get("ok"):
            return r
    return r


# ─── realm helpers ───────────────────────────────────────────────────────────
def strat(slot: int, off: int) -> int:
    return STRAT_BASE + slot * STRAT_STRIDE + off


def rec_owner_va(rec: int) -> int:
    return REC_BASE + rec * REC_STRIDE + R_OWNER


def region_owner_map(max_rec: int = 0x96) -> dict:
    """rec/region-id -> owning realm (record byte +5).  region-id == record idx."""
    specs = [(f"r{k}", rec_owner_va(k), "u8") for k in range(1, max_rec + 1)]
    v = reads(specs)
    return {k: v.get(f"r{k}") for k in range(1, max_rec + 1) if v.get(f"r{k}")}


def human_slot() -> int:
    for s in range(1, 6):
        if read(strat(s, S_HUMAN), "u8") == 1:
            return s
    return 1


def load_region_grid(rescan: bool = False) -> dict:
    """{region_id: [(gx,gy), ...]} minimap pixels.  Static per map → cached."""
    if CACHE.exists() and not rescan:
        raw = json.loads(CACHE.read_text())
        return {int(k): [tuple(p) for p in v] for k, v in raw.items()}
    grid: dict[int, list] = {}
    for gy0 in range(0, MM_H, 16):                 # 16 rows × 128 = 2048/chunk
        specs = [(f"{gx}_{gy}", MMGRID + gy * MM_W + gx, "u8")
                 for gy in range(gy0, gy0 + 16) for gx in range(MM_W)]
        v = reads(specs)
        for gy in range(gy0, gy0 + 16):
            for gx in range(MM_W):
                r = v.get(f"{gx}_{gy}")
                if r:
                    grid.setdefault(r, []).append((gx, gy))
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    CACHE.write_text(json.dumps({str(k): v for k, v in grid.items()}))
    return grid


def owned_center_pixel(slot: int, grid: dict, owners: dict):
    """Minimap pixel to click to recentre on `slot`: the on-territory pixel
    nearest the centroid of all the realm's owned minimap cells."""
    pts = [p for reg, px in grid.items() if owners.get(reg) == slot for p in px]
    if not pts:
        return None, None
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    best = min(pts, key=lambda p: (p[0] - cx) ** 2 + (p[1] - cy) ** 2)
    # region under that pixel = the realm's capital-ish region for curterr
    reg_at = next((reg for reg, px in grid.items()
                   if owners.get(reg) == slot and best in px), None)
    return best, reg_at


# ─── commands ────────────────────────────────────────────────────────────────
def cmd_pov(a):
    slot = a.slot
    grid = load_region_grid(rescan=a.rescan)
    owners = region_owner_map()
    px, reg = owned_center_pixel(slot, grid, owners)
    if px is None:
        print(f"realm slot {slot} owns no territory (eliminated?)", file=sys.stderr)
        return 1
    sx, sy = MM_SCREEN_X0 + px[0], MM_SCREEN_Y0 + px[1]
    click(sx, sy)                       # game's own recentre transform
    sleep_ms(400)
    poke_i32(VIEWER, slot)              # HUD/panel read this realm
    if reg:
        poke_i32(CURTERR, reg)          # select their capital → OWNED info card
    r = force_present_shot(a.shot)
    v = reads([("viewer", VIEWER, "i32"), ("curterr", CURTERR, "i32"),
               ("cash", strat(slot, S_CASH), "i32")])
    print(f"POV → realm slot {slot}: recentred minimap px {px} (region {reg}), "
          f"viewer={v['viewer']} curterr={v['curterr']} treasury={v['cash']}")
    print(f"shot: {a.shot if r.get('ok') else '(no frame) ' + r.get('err','')}")
    if a.restore:
        _restore()
    return 0


def _restore():
    hs = human_slot()
    grid = load_region_grid()
    owners = region_owner_map()
    px, reg = owned_center_pixel(hs, grid, owners)
    if px:
        click(MM_SCREEN_X0 + px[0], MM_SCREEN_Y0 + px[1])
        sleep_ms(400)
    poke_i32(VIEWER, hs)
    if reg:
        poke_i32(CURTERR, reg)
    force_present_shot(None)


def cmd_human(a):
    _restore()
    print(f"restored POV → human (slot {human_slot()})")
    return 0


def cmd_data(a):
    diff = read(DIFFICULTY, "i32")
    ai_div = DIVISOR_BY_DIFF.get(diff, 10)
    print(f"difficulty={diff} → AI upkeep divisor value/{ai_div}, "
          f"human value/{HUMAN_DIVISOR}  (AI pays "
          f"{HUMAN_DIVISOR/ai_div:.0%} of the human rate for an equal army)")
    print(f"{'slot':>4} {'who':>6} {'pers':>4} {'rank':>4} "
          f"{'treasury':>9} {'upkeep/turn':>11} {'eff.div':>7}")
    for s in range(1, 6):
        v = reads([("active", strat(s, S_ACTIVE), "u8"),
                   ("human", strat(s, S_HUMAN), "u8"),
                   ("pers", strat(s, S_PERS), "u8"),
                   ("rank", strat(s, S_RANK), "u8"),
                   ("wage", strat(s, S_WAGE_SUM), "i32"),
                   ("cash", strat(s, S_CASH), "i32")])
        if not v.get("active"):
            continue
        who = "HUMAN" if v["human"] == 1 else "ai"
        div = HUMAN_DIVISOR if v["human"] == 1 else ai_div
        print(f"{s:>4} {who:>6} {v['pers']:>4} {v['rank']:>4} "
              f"{v['cash']:>9} {v['wage']:>11} {'/'+str(div):>7}")
    return 0


def cmd_scan(a):
    grid = load_region_grid(rescan=True)
    owners = region_owner_map()
    print(f"cached {len(grid)} regions → {CACHE}")
    for reg in sorted(grid):
        o = owners.get(reg)
        tag = f"owner {o}" + (" (player)" if o and o < 5 else "")
        print(f"  region {reg:>2}: {len(grid[reg]):>4} px  {tag}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("pov", help="switch POV to a realm slot + capture")
    p.add_argument("slot", type=int)
    p.add_argument("--shot", default=None, help="capture PNG to this path")
    p.add_argument("--restore", action="store_true", help="restore human POV after")
    p.add_argument("--rescan", action="store_true", help="rebuild region-grid cache")
    p.set_defaults(func=cmd_pov)

    p = sub.add_parser("human", aliases=["restore"], help="restore the human POV")
    p.set_defaults(func=cmd_human)

    p = sub.add_parser("data", help="per-realm economy + upkeep-cheat table")
    p.set_defaults(func=cmd_data)

    p = sub.add_parser("scan", help="(re)build the minimap region-grid cache")
    p.set_defaults(func=cmd_scan)

    a = ap.parse_args()
    sys.exit(a.func(a))


if __name__ == "__main__":
    main()
