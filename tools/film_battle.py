#!/usr/bin/env python3
"""
tools/film_battle.py — film a played-out LoTR2 tactical battle for VIDEO beat #2
("the tactical AI is exploitably dumb → why the devs rig the auto-calc").

The stepped-capture pipeline that finally makes battle footage reliable:
  • enter a driveable battle (enter_battle) or stage a clean kiting fight
    (kiting_demo: pure archers vs a melee class),
  • drive a KITING commander (archers fire from range; the enemy melts while
    our archers take zero damage — the verified "ranged never kite" exploit),
  • FOLLOW-CAM: poke the battlefield camera (micro_bot.center_on/follow) onto
    the live-unit centroid so the action stays framed,
  • capture each step with the LOCK-FREE direct framebuffer read
    (daemon `shotd` → agent probeShotDirect → *DAT_004ea1a8), which works while
    the sim is FROZEN — so we freeze → render → read → order → step, with NO
    concurrent `record` streamer (that combo crashed frida) and NO DDraw Lock
    race.  Frames → ffmpeg → mp4.

Two timing fixes that make the capture clean (learned 2026-07-10):
  1. after `start`, let the game RUN briefly so the combat palette loads (a
     capture before that reads a stale/dark palette),
  2. after poking the battlefield redraw counter, SETTLE ~0.15 s so the tick
     actually re-renders into the back buffer before the direct read.

Usage (daemon up, retail on cutestation):
    nix develop -c python3 tools/film_battle.py --enemy mace --n 40 --rounds 18 \
        --out runs/probe/beat2/kite.mp4
    nix develop -c python3 tools/film_battle.py --natural   # real 28v28, charge
"""
from __future__ import annotations
import argparse, os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import enter_battle as eb
import micro_bot as mb
import kiting_demo as kd

V_SCREEN, V_PAUSE, V_BFREDRAW = 0x4eac50, 0x53f238, 0x53e9ac


class Film:
    def __init__(self, port=27099):
        self.d = eb.Daemon(port)
        self.b = mb.Battle(mb.Daemon(port))
        self.kd = kd.Demo(port)

    def freeze(self):
        self.d.poke(V_PAUSE, [255, 255, 255, 255])

    def step(self, ms):
        """Advance the sim a short controlled window, then re-freeze."""
        self.d.poke(V_SCREEN, [0x29]); self.d.poke(V_PAUSE, [0, 0, 0, 0])
        self.d.rpc({"cmd": "sleep", "ms": ms})
        self.freeze()

    def screen(self):
        return self.d.rd(V_SCREEN, "u8")

    def shot(self, path, settle_ms=150):
        """Lock-free direct capture of the current (even frozen) frame."""
        self.d.poke(V_BFREDRAW, [8, 0, 0, 0])          # force a battlefield re-render
        self.d.rpc({"cmd": "sleep", "ms": settle_ms})   # let the tick render it
        return self.d.rpc({"cmd": "shotd", "path": path}).get("ok")


def units_by_side(us):
    return ([u for u in us if u["side"] == 0], [u for u in us if u["side"] == 1])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--enemy", default="mace", choices=list(kd.CLASS))
    ap.add_argument("--n", type=int, default=40, help="troops/side for the staged kite (small)")
    ap.add_argument("--natural", action="store_true", help="real 28v28 (enter_battle 4v5) + charge, no staging")
    ap.add_argument("--attacker", type=int, default=4)
    ap.add_argument("--defender", type=int, default=5)
    ap.add_argument("--rounds", type=int, default=18)
    ap.add_argument("--step-ms", type=int, default=350)
    ap.add_argument("--fire-row", type=int, default=20, help="staged archers' firing row")
    ap.add_argument("--out", default="runs/probe/beat2/battle.mp4")
    ap.add_argument("--fps", type=int, default=6, help="output mp4 fps (one frame per round)")
    ap.add_argument("--port", type=int, default=27099)
    a = ap.parse_args()

    f = Film(a.port)
    framedir = a.out.rsplit(".", 1)[0] + "_frames"
    os.makedirs(framedir, exist_ok=True)

    if a.natural:
        eb.enter(f.d, a.attacker, a.defender); time.sleep(0.3)
        f.b.start(); time.sleep(0.5)                    # RUN → combat palette loads
        commander = lambda us: mb.commander_charge(f.b, us)
    else:
        print(f"staging {a.n} archers vs {a.n} {a.enemy}...")
        f.kd.stage({kd.CLASS["archer"]: a.n}, {kd.CLASS[a.enemy]: a.n})
        us = f.kd.units(); mine, foe = units_by_side(us)
        print(f"deployed {len(mine)} archer stacks vs {len(foe)} {a.enemy} stacks")
        if not mine or not foe:
            print("deploy failed (try smaller --n)"); return
        f.b.start(); time.sleep(0.5)                    # RUN → combat palette loads
        for k, u in enumerate(mine):
            f.kd.setpos(u["idx"], 51 + k, a.fire_row)   # firing line near the enemy spawn
        def commander(us):
            _, foe = units_by_side(us)
            for frm in sorted({u["form"] for u in us if u["side"] == 0}):
                tf = kd.nearest({"col": 55, "row": a.fire_row}, foe)
                if tf:
                    f.b.order(frm, tf["col"], tf["row"], tf["idx"])

    f.freeze()
    log = []
    for rnd in range(a.rounds):
        us = f.b.units(); mine, foe = units_by_side(us)
        myhp = sum(u["hp"] for u in mine); fhp = sum(u["hp"] for u in foe)
        sc = f.screen()
        f.b.follow(us)
        ok = f.shot(os.path.join(framedir, f"f{rnd:03d}.png"))
        log.append((rnd, myhp, fhp))
        print(f"r{rnd:2d}: usHP={myhp:4d} enemyHP={fhp:4d} scr=0x{sc:x} cap={ok}")
        if not foe or fhp == 0:
            print("-> enemy destroyed"); break
        if not mine or sc < 0x28 or sc > 0x2a:
            print(f"-> battle ended (scr=0x{sc:x})"); break
        commander(us)
        f.step(a.step_ms)
    f.freeze()

    # assemble → mp4
    n = len([x for x in os.listdir(framedir) if x.endswith(".png")])
    if n:
        subprocess.run(
            ["ffmpeg", "-y", "-framerate", str(a.fps), "-i", os.path.join(framedir, "f%03d.png"),
             "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", a.out],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"\nassembled {n} frames → {a.out}")
    if log:
        print(f"fight: us {log[0][1]}→{log[-1][1]} | enemy {log[0][2]}→{log[-1][2]}")


if __name__ == "__main__":
    main()
