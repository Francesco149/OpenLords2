#!/usr/bin/env python3
"""
tools/enter_battle.py — enter a DRIVEABLE, PAUSED interactive tactical battle
on live retail, between two strategic combat records (g_per_territory_420).

The reverse-engineered entry (see docs/findings/combat-battlefield-input.md):
the raw `FUN_004263ad+FUN_0042b919+FUN_0042b7f7` sequence is the AUTO-RESOLVE
path (DAT_0055302c==3) — it musters + loads the map but never deploys units.
The INTERACTIVE deploy is `FUN_0047efee`, reached ONLY via `FUN_004778a0`
(the battle-scene entry). So the working recipe is:

    FUN_0042b919()   # combat sub-mode init (DAT_0053e91c=0 interactive)
    FUN_0042b7f7()   # mode=2, camera, muster recs 1/2, load battlefield terrain
    FUN_004778a0()   # <-- THE DEPLOY: FUN_0047efee places units, screen -> 0x29

then poke screen `DAT_004eac50 = 0x28` to PAUSE at the deploy phase ("Battle
Paused") — units stay put, the sim is frozen, and it renders. That is the
control window for a micro/kiting bot: set orders at 0x28, flip to 0x29 to let
them execute for N ticks, flip back to 0x28. The battle sim runs each tick when
`DAT_0057c8fc==2 && DAT_004eac50 < 0x2b` (main loop @all.c:88791).

Proven live 2026-07-09: attacker rec4 vs defender rec5 -> 56 units (28 vs 28),
"Battle Paused", stable. Requires the probe daemon up (default port 27099).

Usage:
    python tools/enter_battle.py --attacker 4 --defender 5 [--port 27099]
    python tools/enter_battle.py --attacker 4 --defender 5 --start   # unpause->fight
"""
from __future__ import annotations
import argparse, json, socket, time

# ── globals / functions (VAs) ────────────────────────────────────────────────
V_VIEWER   = 0x57c8cc   # local player id (1 = human)
V_QUICKOPT = 0x53f284   # quick-combat option (1 = fight interactively)
V_ATK      = 0x568210   # attacker combat-record index
V_DEF      = 0x568218   # defender combat-record index
V_ROLE     = 0x5533d0   # local player's combat role (1=attacker)
V_SCREEN   = 0x4eac50   # current screen (0x28 deploy / 0x29 fight)
V_MODE     = 0x57c8fc   # game mode (2 = combat)
V_BFREDRAW = 0x53e9ac   # battlefield force-redraw counter
F_CLEAR    = 0x4aa89f   # engine_combat_clear_battle_result(terr)
F_MUSTER   = 0x422c12   # load_muster(attacker, defender)
F_SUBINIT  = 0x42b919   # combat sub-mode init
F_ENTER    = 0x42b7f7   # combat-mode setup (mode 2, camera, terrain, scratch recs 1/2)
F_DEPLOY   = 0x4778a0   # battle-scene entry -> FUN_0047efee deploys units
BASE, STRIDE = 0x52f0b0, 0x1a4     # g_per_territory_420
UT = 0x5544ac                       # battlefield unit table base (0x50 recs, stride 0x1b0)

def rec(i): return BASE + i * STRIDE

class Daemon:
    def __init__(self, port): self.port = port
    def rpc(self, req):
        s = socket.socket(); s.connect(("127.0.0.1", self.port))
        s.sendall((json.dumps(req) + "\n").encode()); buf = b""
        while b"\n" not in buf:
            c = s.recv(65536)
            if not c: break
            buf += c
        return json.loads(buf.split(b"\n")[0].decode())
    def rd(self, va, ty="i32"): return self.rpc({"cmd": "read", "va": va, "type": ty})["val"]
    def poke(self, va, data): self.rpc({"cmd": "poke", "va": va, "bytes": data if isinstance(data, list) else [data]})
    def pi32(self, va, v): self.poke(va, [v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff])
    def callq(self, va, *a): return self.rpc({"cmd": "callq", "va": va, "args": list(a)}).get("ret")

def live_units(d):
    specs = [{"name": f"c{i}", "va": UT + i * 0x1b0, "type": "u8"} for i in range(1, 0x51)]
    specs += [{"name": f"o{i}", "va": UT + i * 0x1b0 + 0x02, "type": "u8"} for i in range(1, 0x51)]
    v = d.rpc({"cmd": "reads", "specs": specs})["vals"]
    sides = {}
    for i in range(1, 0x51):
        if v[f"c{i}"] != 0:
            sides[v[f"o{i}"]] = sides.get(v[f"o{i}"], 0) + 1
    return sides

def enter(d, attacker, defender, start=False):
    # 1. human-flags: attacker human, defender AI (gates size!=0 + role)
    d.poke(rec(attacker) + 1, [1]); d.poke(rec(defender) + 1, [0])
    d.pi32(V_VIEWER, 1); d.pi32(V_QUICKOPT, 1)
    # 2. combatants + clear result accumulators
    d.pi32(V_ATK, attacker); d.pi32(V_DEF, defender)
    d.callq(F_CLEAR, attacker); d.callq(F_CLEAR, defender)
    d.pi32(V_ROLE, 1)
    # 3. muster the two armies into the combat scratch
    d.callq(F_MUSTER, attacker, defender)
    # 4. sub-mode init + combat-mode setup (musters scratch recs 1/2 + loads terrain)
    d.callq(F_SUBINIT); d.callq(F_ENTER)
    # 5. THE DEPLOY — places units on the hex grid, screen -> 0x29
    d.callq(F_DEPLOY)
    time.sleep(0.3)
    if not start:
        d.poke(V_SCREEN, [0x28])            # pause at deploy phase ("Battle Paused")
    d.poke(V_BFREDRAW, [8, 0, 0, 0])
    sides = live_units(d)
    print(f"screen=0x{d.rd(V_SCREEN,'u8'):x} mode={d.rd(V_MODE)} units_by_side={sides} "
          f"total={sum(sides.values())} {'(FIGHTING)' if start else '(PAUSED at deploy)'}")
    return sides

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--attacker", type=int, required=True, help="attacker combat-record index (human)")
    ap.add_argument("--defender", type=int, required=True, help="defender combat-record index (AI)")
    ap.add_argument("--port", type=int, default=27099)
    ap.add_argument("--start", action="store_true", help="unpause into the fight (screen 0x29) instead of pausing")
    args = ap.parse_args()
    enter(Daemon(args.port), args.attacker, args.defender, args.start)
