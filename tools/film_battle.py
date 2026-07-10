#!/usr/bin/env python3
"""
tools/film_battle.py — film a played-out LoTR2 tactical battle for VIDEO beat #2
("the tactical AI is exploitably dumb → why the devs rig the auto-calc").

WORKING RECIPE (2026-07-10, produced the first clean 28v28 clip — no crash):
  • NATURAL 28v28 (enter_battle 4v5, 800 HP/side): the compelling, STABLE fight.
  • Drive with a CHARGE of **move-orders** (FUN_00479e90 target=0). Move-orders are
    stable for 100+ rounds; **attack-orders (target=enemy) and mass-`setpos` crash
    frida**, and fast routs (peasant / close archers) crash it too — so the big
    slow-attrition melee is the reliable one.
  • Capture DURING THE RUN (screen 0x29, present-active) with the LOCK-FREE direct
    framebuffer read (`shotd` → *DAT_004ea1a8). Running frames read the correct
    BRIGHT palette (frozen-mid-combat frames read a dark palette cycle-state — a game
    palette quirk both capture paths see, avoided by capturing while running). No
    freeze, no `record` streamer (that combo crashed frida).
  • FOLLOW-CAM on OUR army's centroid (side 0) via micro_bot.center_on — the camera
    tracks our army the whole march across the field (a rectangular tile grid; poke
    DAT_0055ce70/DAT_005678a0). Following the *global* centroid frames the empty
    midpoint while the armies are still apart, so follow side 0.

Result: our 28-stack army marches the whole field (frontRow 70→4), crosses the
bridge chokepoint, and clashes — 120 bright frames → mp4, no crash.

  STAGED kiting mode (--staged) is EXPERIMENTAL: pure archers vs a melee class via
  kiting_demo staging + fire (attack) orders. Numerically proves the kite but is
  crash-prone (attack-orders on an in-range/routing enemy drop frida). Kept for the
  isolated-mechanic shot; drive gently, expect reboots.

Usage (daemon up, retail on cutestation):
    nix develop -c python3 tools/film_battle.py --out runs/probe/beat2/march.mp4
    nix develop -c python3 tools/film_battle.py --staged --enemy mace --n 16
"""
from __future__ import annotations
import argparse, os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import enter_battle as eb
import micro_bot as mb
import kiting_demo as kd

V_SCREEN = 0x4eac50


class Film:
    def __init__(self, port=27099):
        self.d = eb.Daemon(port)
        self.b = mb.Battle(mb.Daemon(port))
        self.kd = kd.Demo(port)

    def screen(self):
        return self.d.rd(V_SCREEN, "u8")

    def follow_side(self, us, side=0, ahead_col=3, ahead_row=-2):
        """Keep the camera on `side`'s live-unit centroid, biased slightly toward
        the enemy so the leading edge (and the clash) stays framed."""
        live = [u for u in us if u["side"] == side and u["hp"] > 0]
        if live:
            self.b.center_on(sum(u["col"] for u in live) / len(live) + ahead_col,
                             sum(u["row"] for u in live) / len(live) + ahead_row)

    def shotd(self, path):
        """Lock-free direct capture of the CURRENT running frame (bright palette)."""
        return self.d.rpc({"cmd": "shotd", "path": path}).get("ok")


def hp(us, side):
    return sum(u["hp"] for u in us if u["side"] == side)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--staged", action="store_true", help="EXPERIMENTAL pure-archer kite (crash-prone); default = natural 28v28")
    ap.add_argument("--enemy", default="mace", choices=list(kd.CLASS), help="staged enemy class")
    ap.add_argument("--n", type=int, default=16, help="staged troops/side (small!)")
    ap.add_argument("--attacker", type=int, default=4)
    ap.add_argument("--defender", type=int, default=5)
    ap.add_argument("--rounds", type=int, default=120)
    ap.add_argument("--sleep-ms", type=int, default=230, help="sim-advance per captured frame")
    ap.add_argument("--recharge", type=int, default=6, help="re-issue the move-charge every N rounds")
    ap.add_argument("--budget-s", type=float, default=95, help="wall-clock cap")
    ap.add_argument("--out", default="runs/probe/beat2/march.mp4")
    ap.add_argument("--fps", type=int, default=12)
    ap.add_argument("--port", type=int, default=27099)
    a = ap.parse_args()

    f = Film(a.port)
    fdir = a.out.rsplit(".", 1)[0] + "_frames"
    os.makedirs(fdir, exist_ok=True)
    for x in os.listdir(fdir):
        if x.endswith(".png"):
            os.remove(os.path.join(fdir, x))

    if a.staged:
        print(f"[staged/EXPERIMENTAL] {a.n} archers vs {a.n} {a.enemy}...")
        f.kd.stage({kd.CLASS["archer"]: a.n}, {kd.CLASS[a.enemy]: a.n})
        us = f.kd.units(); mine = [u for u in us if u["side"] == 0]
        if not mine:
            print("deploy failed (try smaller --n)"); return
        f.b.start(); time.sleep(0.4)
        for k, u in enumerate(mine):
            f.kd.setpos(u["idx"], 51 + k, 20)          # firing line (row 20 = stable distance)
        def order():
            foe = [u for u in f.b.units() if u["side"] == 1]
            for frm in sorted({u["form"] for u in f.b.units() if u["side"] == 0}):
                tf = kd.nearest({"col": 55, "row": 20}, foe)
                if tf:
                    f.b.order(frm, tf["col"], tf["row"], tf["idx"])   # attack (crash-prone)
        follow_side = 0
    else:
        eb.enter(f.d, a.attacker, a.defender); time.sleep(0.3)
        f.b.start(); time.sleep(0.4)                    # 0x29 → bright palette
        mb.commander_charge(f.b, f.b.units())           # move-orders toward the enemy centroid
        def order():
            mb.commander_charge(f.b, f.b.units())
        follow_side = 0

    n = ended = 0
    t0 = time.time()
    for i in range(a.rounds):
        if time.time() - t0 > a.budget_s:
            print("wall budget reached"); break
        us = f.b.units(); sc = f.screen()
        s0, s1 = hp(us, 0), hp(us, 1)
        front = min((u["row"] for u in us if u["side"] == 0 and u["hp"] > 0), default=99)
        f.follow_side(us, follow_side)
        if f.shotd(os.path.join(fdir, f"f{n:03d}.png")):
            n += 1
        if i % 5 == 0:
            print(f"i{i:3d}: us={s0:4d} en={s1:4d} frontRow={front:2d} scr=0x{sc:x} f={n}")
        if sc < 0x28 or sc > 0x2a:
            ended += 1
            if ended > 6:
                print(f"battle ended (scr=0x{sc:x})"); break
        elif i % a.recharge == a.recharge - 1:
            order()
        f.d.rpc({"cmd": "sleep", "ms": a.sleep_ms})

    if n:
        subprocess.run(
            ["ffmpeg", "-y", "-framerate", str(a.fps), "-i", os.path.join(fdir, "f%03d.png"),
             "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", a.out],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"assembled {n} frames → {a.out}")


if __name__ == "__main__":
    main()
